/**
 * @file stepper_hardware_test.c
 * @brief 通过蓝牙命令安全触发并监视 ZDT X42S 单电机硬件测试。
 * @note 本文件仅用于测试构建。上电不自动使能或运动，必须收到 @stepper_run# 才开始。
 * @warning 测试前必须确保电机轴附近无人员和易损物，并预留正反方向机械行程。
 */
#include "stepper_hardware_test.h"
#include "bluetooth_service.h"
#include "bsp_board.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STEPPER_TEST_MOTOR_ID 1U /* 被测单电机地址 */
#define STEPPER_TEST_INVALID_ID 2U /* 越界 ID 防御测试值 */
#define STEPPER_TEST_SPEED_RPM_X10 600U /* 运动速度 60.0 RPM */
#define STEPPER_TEST_ACCELERATION_RPM_S 300U /* 加减速度 300 RPM/s */
#define STEPPER_TEST_RESPONSE_TIMEOUT_MS 500U /* 命令应答超时 */
#define STEPPER_TEST_POSITION_TIMEOUT_MS 500U /* 实时位置应答超时 */
#define STEPPER_TEST_SHORT_SETTLE_MS 150U /* 非运动命令稳定等待时间 */
#define STEPPER_TEST_ZERO_SETTLE_MS 300U /* 零角度命令观察时间 */
#define STEPPER_TEST_MOVE_SETTLE_MS 1000U /* 每次小角度运动观察时间 */
#define STEPPER_TEST_STOP_TRIGGER_MS 100U /* 长行程开始后发送停止命令的延时 */
#define STEPPER_TEST_COMMAND_ENABLE 0xF3U /* 使能应答功能码 */
#define STEPPER_TEST_COMMAND_MOVE 0xFDU /* 位置运动应答功能码 */
#define STEPPER_TEST_COMMAND_STOP 0xFEU /* 立即停止应答功能码 */
#define STEPPER_TEST_COMMAND_CLEAR_POSITION 0x0AU /* 位置清零应答功能码 */
#define STEPPER_TEST_RESPONSE_OK 0x02U /* 命令正确接收返回码 */
#define STEPPER_TEST_POSITION_TOLERANCE_DEG 1.0F /* 实测位置允许误差，单位：度 */

/* 测试步骤执行动作 */
typedef enum {
    STEPPER_TEST_ACTION_ENABLE = 0, /* 通过 UART 使能电机 */
    STEPPER_TEST_ACTION_CLEAR_POSITION, /* 将当前位置设置为坐标零点 */
    STEPPER_TEST_ACTION_MOVE,       /* 发送一条 FD 位置命令 */
    STEPPER_TEST_ACTION_STOP,       /* 发送立即停止命令 */
    STEPPER_TEST_ACTION_DISABLE,    /* 通过 UART 失能电机 */
} stepper_test_action_t;

/* 非阻塞测试状态机状态 */
typedef enum {
    STEPPER_TEST_STATE_IDLE = 0,    /* 等待蓝牙启动命令 */
    STEPPER_TEST_STATE_PREFLIGHT,   /* 执行不产生运动的接口检查 */
    STEPPER_TEST_STATE_SEND,        /* 发送当前测试步骤 */
    STEPPER_TEST_STATE_WAIT_ACK,    /* 等待电机控制命令应答 */
    STEPPER_TEST_STATE_WAIT_MOTION, /* 等待当前小角度运动结束 */
    STEPPER_TEST_STATE_SEND_POSITION_READ, /* 发送实时位置读取命令 */
    STEPPER_TEST_STATE_WAIT_POSITION, /* 等待并校验实时位置应答 */
    STEPPER_TEST_STATE_POSTFLIGHT,  /* 执行失能后的接口检查 */
    STEPPER_TEST_STATE_ABORT_STOP,  /* 中止时重试立即停止命令 */
    STEPPER_TEST_STATE_ABORT_DISABLE, /* 中止时重试失能命令 */
    STEPPER_TEST_STATE_COMPLETE,      /* 测试结束，等待再次启动 */
} stepper_test_state_t;

/* 测试步骤完成后的实时位置检查方式 */
typedef enum {
    STEPPER_TEST_POSITION_CHECK_NONE = 0, /* 本步骤不读取实时位置 */
    STEPPER_TEST_POSITION_CHECK_EXACT,    /* 实测位置应位于目标容差内 */
    STEPPER_TEST_POSITION_CHECK_BELOW,    /* 实测位置应小于未停止时的目标 */
} stepper_test_position_check_t;

/* 一条可执行的硬件测试步骤 */
typedef struct {
    const char *name;                    /* 蓝牙日志中的步骤名称 */
    stepper_test_action_t action;        /* 本步骤执行动作 */
    float angle_deg;                     /* 位置动作角度，单位：度 */
    bsp_stepper_move_mode_t mode;        /* 位置动作模式 */
    uint8_t expected_command;            /* 预期应答功能码 */
    uint32_t settle_ms;                  /* 应答后安全等待时间 */
    float expected_position_deg;         /* 步骤完成后的预期坐标，单位：度 */
    stepper_test_position_check_t position_check; /* 实时位置检查方式 */
} stepper_test_step_t;

static const stepper_test_step_t test_steps[] = {
    {"enable", STEPPER_TEST_ACTION_ENABLE, 0.0F, BSP_STEPPER_MODE_RELATIVE_CURRENT,
        STEPPER_TEST_COMMAND_ENABLE, STEPPER_TEST_SHORT_SETTLE_MS, 0.0F,
        STEPPER_TEST_POSITION_CHECK_NONE},
    {"clear-position", STEPPER_TEST_ACTION_CLEAR_POSITION, 0.0F,
        BSP_STEPPER_MODE_RELATIVE_CURRENT, STEPPER_TEST_COMMAND_CLEAR_POSITION,
        STEPPER_TEST_SHORT_SETTLE_MS, 0.0F, STEPPER_TEST_POSITION_CHECK_EXACT},
    {"zero-relative-current", STEPPER_TEST_ACTION_MOVE, 0.0F,
        BSP_STEPPER_MODE_RELATIVE_CURRENT, STEPPER_TEST_COMMAND_MOVE,
        STEPPER_TEST_ZERO_SETTLE_MS, 0.0F, STEPPER_TEST_POSITION_CHECK_EXACT},
    {"cw-45-relative-current", STEPPER_TEST_ACTION_MOVE, 45.0F,
        BSP_STEPPER_MODE_RELATIVE_CURRENT, STEPPER_TEST_COMMAND_MOVE,
        STEPPER_TEST_MOVE_SETTLE_MS, 45.0F, STEPPER_TEST_POSITION_CHECK_EXACT},
    {"ccw-45-relative-current", STEPPER_TEST_ACTION_MOVE, -45.0F,
        BSP_STEPPER_MODE_RELATIVE_CURRENT, STEPPER_TEST_COMMAND_MOVE,
        STEPPER_TEST_MOVE_SETTLE_MS, 0.0F, STEPPER_TEST_POSITION_CHECK_EXACT},
    {"cw-30-relative-target", STEPPER_TEST_ACTION_MOVE, 30.0F,
        BSP_STEPPER_MODE_RELATIVE_TARGET, STEPPER_TEST_COMMAND_MOVE,
        STEPPER_TEST_MOVE_SETTLE_MS, 30.0F, STEPPER_TEST_POSITION_CHECK_EXACT},
    {"ccw-30-relative-target", STEPPER_TEST_ACTION_MOVE, -30.0F,
        BSP_STEPPER_MODE_RELATIVE_TARGET, STEPPER_TEST_COMMAND_MOVE,
        STEPPER_TEST_MOVE_SETTLE_MS, 0.0F, STEPPER_TEST_POSITION_CHECK_EXACT},
    {"cw-15-absolute", STEPPER_TEST_ACTION_MOVE, 15.0F, BSP_STEPPER_MODE_ABSOLUTE,
        STEPPER_TEST_COMMAND_MOVE, STEPPER_TEST_MOVE_SETTLE_MS, 15.0F,
        STEPPER_TEST_POSITION_CHECK_EXACT},
    {"zero-absolute", STEPPER_TEST_ACTION_MOVE, 0.0F, BSP_STEPPER_MODE_ABSOLUTE,
        STEPPER_TEST_COMMAND_MOVE, STEPPER_TEST_MOVE_SETTLE_MS, 0.0F,
        STEPPER_TEST_POSITION_CHECK_EXACT},
    {"start-stop-motion", STEPPER_TEST_ACTION_MOVE, 180.0F,
        BSP_STEPPER_MODE_RELATIVE_CURRENT, STEPPER_TEST_COMMAND_MOVE,
        STEPPER_TEST_STOP_TRIGGER_MS, 0.0F, STEPPER_TEST_POSITION_CHECK_NONE},
    {"stop-active-motion", STEPPER_TEST_ACTION_STOP, 0.0F,
        BSP_STEPPER_MODE_RELATIVE_CURRENT, STEPPER_TEST_COMMAND_STOP,
        STEPPER_TEST_MOVE_SETTLE_MS, 170.0F, STEPPER_TEST_POSITION_CHECK_BELOW},
    {"return-zero-after-stop", STEPPER_TEST_ACTION_MOVE, 0.0F, BSP_STEPPER_MODE_ABSOLUTE,
        STEPPER_TEST_COMMAND_MOVE, STEPPER_TEST_MOVE_SETTLE_MS, 0.0F,
        STEPPER_TEST_POSITION_CHECK_EXACT},
    {"disable", STEPPER_TEST_ACTION_DISABLE, 0.0F, BSP_STEPPER_MODE_RELATIVE_CURRENT,
        STEPPER_TEST_COMMAND_ENABLE, STEPPER_TEST_SHORT_SETTLE_MS, 0.0F,
        STEPPER_TEST_POSITION_CHECK_NONE},
};

static stepper_test_state_t test_state;
static uint32_t deadline_ms;
static uint32_t response_sequence_before_send;
static uint32_t position_sequence_before_read;
static uint16_t passed_checks;
static uint16_t failed_checks;
static uint8_t step_index;

/**
 * @brief  判断一个允许自然回绕的毫秒截止时间是否已经到达
 * @param  now 当前单调毫秒时间
 * @param  deadline 目标截止时间
 * @return 当前时间达到或越过截止时间时返回 true
 */
static bool deadline_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

/**
 * @brief  记录一次状态码断言并通过蓝牙打印结果
 * @param  name 断言名称
 * @param  actual 接口实际状态码
 * @param  expected 接口预期状态码
 */
static void report_check(const char *name, status_code_t actual, status_code_t expected)
{
    if (actual == expected) {
        passed_checks++;
        (void)bluetooth_service_printf("[stepper-test] PASS %s status=%u\r\n", name,
            (unsigned int)actual);
    } else {
        failed_checks++;
        (void)bluetooth_service_printf("[stepper-test] FAIL %s actual=%u expected=%u\r\n", name,
            (unsigned int)actual, (unsigned int)expected);
    }
}

/**
 * @brief  执行当前测试表中的一条命令
 * @param  step 待执行测试步骤
 * @return BSP 步进电机接口返回的状态码
 */
static status_code_t execute_step(const stepper_test_step_t *step)
{
    switch (step->action) {
        case STEPPER_TEST_ACTION_ENABLE:
            return bsp_stepper_enable(STEPPER_TEST_MOTOR_ID, true);
        case STEPPER_TEST_ACTION_CLEAR_POSITION:
            return bsp_stepper_clear_position(STEPPER_TEST_MOTOR_ID);
        case STEPPER_TEST_ACTION_MOVE:
            return bsp_stepper_move(STEPPER_TEST_MOTOR_ID, step->angle_deg,
                STEPPER_TEST_SPEED_RPM_X10, STEPPER_TEST_ACCELERATION_RPM_S, step->mode, false);
        case STEPPER_TEST_ACTION_STOP:
            return bsp_stepper_stop(STEPPER_TEST_MOTOR_ID);
        case STEPPER_TEST_ACTION_DISABLE:
            return bsp_stepper_enable(STEPPER_TEST_MOTOR_ID, false);
        default:
            return STATUS_INVALID_ARGUMENT;
    }
}

/**
 * @brief  执行不会产生电机运动的前置接口检查
 */
static void run_preflight(void)
{
    bsp_stepper_position_t position = {0};
    bsp_stepper_response_t response = {0};
    status_code_t status;

    report_check("invalid-id", bsp_stepper_enable(STEPPER_TEST_INVALID_ID, false),
        STATUS_OUT_OF_RANGE);
    report_check("move-while-disabled",
        bsp_stepper_move(STEPPER_TEST_MOTOR_ID, 1.0F, STEPPER_TEST_SPEED_RPM_X10,
            STEPPER_TEST_ACCELERATION_RPM_S, BSP_STEPPER_MODE_RELATIVE_CURRENT, false),
        STATUS_STATE_ERROR);
    report_check("invalid-mode",
        bsp_stepper_move(STEPPER_TEST_MOTOR_ID, 1.0F, STEPPER_TEST_SPEED_RPM_X10,
            STEPPER_TEST_ACCELERATION_RPM_S, (bsp_stepper_move_mode_t)0xFFU, false),
        STATUS_OUT_OF_RANGE);
    report_check("null-response", bsp_stepper_response(STEPPER_TEST_MOTOR_ID, NULL),
        STATUS_INVALID_ARGUMENT);
    report_check("null-position", bsp_stepper_position(STEPPER_TEST_MOTOR_ID, NULL),
        STATUS_INVALID_ARGUMENT);
    status = bsp_stepper_response(STEPPER_TEST_MOTOR_ID, &response);
    if ((status == STATUS_OK) || (status == STATUS_UNAVAILABLE)) {
        report_check("response-api", status, status);
    } else {
        report_check("response-api", status, STATUS_OK);
    }
    status = bsp_stepper_position(STEPPER_TEST_MOTOR_ID, &position);
    if ((status == STATUS_OK) || (status == STATUS_UNAVAILABLE)) {
        report_check("position-api", status, status);
    } else {
        report_check("position-api", status, STATUS_OK);
    }
}

/**
 * @brief  处理蓝牙启动测试命令
 * @param  name 命令名称
 * @param  value 当前命令未使用的值字段
 * @param  context 当前命令未使用的上下文
 */
static void on_run_command(const char *name, const char *value, void *context)
{
    (void)name;
    (void)value;
    (void)context;
    if ((test_state != STEPPER_TEST_STATE_IDLE) &&
        (test_state != STEPPER_TEST_STATE_COMPLETE)) {
        (void)bluetooth_service_printf("[stepper-test] BUSY state=%u\r\n",
            (unsigned int)test_state);
        return;
    }
    step_index = 0U;
    passed_checks = 0U;
    failed_checks = 0U;
    test_state = STEPPER_TEST_STATE_PREFLIGHT;
    (void)bluetooth_service_printf("[stepper-test] START speed=60.0RPM accel=300RPM/s\r\n");
}

/**
 * @brief  处理蓝牙安全中止命令
 * @param  name 命令名称
 * @param  value 当前命令未使用的值字段
 * @param  context 当前命令未使用的上下文
 */
static void on_stop_command(const char *name, const char *value, void *context)
{
    (void)name;
    (void)value;
    (void)context;
    test_state = STEPPER_TEST_STATE_ABORT_STOP;
    (void)bluetooth_service_printf("[stepper-test] ABORT requested\r\n");
}

/**
 * @brief  处理蓝牙状态查询命令
 * @param  name 命令名称
 * @param  value 当前命令未使用的值字段
 * @param  context 当前命令未使用的上下文
 */
static void on_status_command(const char *name, const char *value, void *context)
{
    bsp_stepper_position_t position = {0};
    bsp_stepper_response_t response = {0};
    status_code_t position_status;
    status_code_t response_status;

    (void)name;
    (void)value;
    (void)context;
    response_status = bsp_stepper_response(STEPPER_TEST_MOTOR_ID, &response);
    position_status = bsp_stepper_position(STEPPER_TEST_MOTOR_ID, &position);
    (void)bluetooth_service_printf(
        "[stepper-test] state=%u step=%u pass=%u fail=%u ack=%u cmd=%02X code=%02X "
        "seq=%lu pos_status=%u pos=%.2f pos_seq=%lu\r\n",
        (unsigned int)test_state, (unsigned int)step_index, (unsigned int)passed_checks,
        (unsigned int)failed_checks, (unsigned int)response_status,
        (unsigned int)response.command, (unsigned int)response.code,
        (unsigned long)response.sequence, (unsigned int)position_status,
        (double)position.angle_deg, (unsigned long)position.sequence);
}

/**
 * @brief  初始化蓝牙步进电机硬件测试命令
 * @retval STATUS_OK 测试命令已经注册
 * @retval STATUS_OUT_OF_RANGE 蓝牙命令表容量不足或名称非法
 * @retval STATUS_BUSY 蓝牙发送队列空间不足
 */
status_code_t stepper_hardware_test_init(void)
{
    status_code_t status;

    bluetooth_service_init();
    status = bsp_bluetooth_bind("stepper_run", on_run_command, NULL);
    if (status != STATUS_OK) {
        return status;
    }
    status = bsp_bluetooth_bind("stepper_stop", on_stop_command, NULL);
    if (status != STATUS_OK) {
        return status;
    }
    status = bsp_bluetooth_bind("stepper_status", on_status_command, NULL);
    if (status != STATUS_OK) {
        return status;
    }
    test_state = STEPPER_TEST_STATE_IDLE;
    return bluetooth_service_printf(
        "[stepper-test] READY @stepper_run# @stepper_stop# @stepper_status#\r\n");
}

/**
 * @brief  非阻塞推进一次蓝牙步进电机硬件测试
 * @retval STATUS_OK 测试状态机和蓝牙发送正常推进
 * @retval STATUS_NOT_INITIALIZED 蓝牙服务尚未初始化
 * @retval STATUS_IO_ERROR 蓝牙 DMA 发送失败
 */
status_code_t stepper_hardware_test_process(void)
{
    const stepper_test_step_t *step;
    bsp_stepper_position_t position = {0};
    bsp_stepper_response_t response = {0};
    status_code_t status;
    uint32_t now = bsp_time_get_ms();
    float position_error_deg;
    bool is_position_valid;

    status = bluetooth_service_process();
    if ((status != STATUS_OK) && (status != STATUS_BUSY)) {
        return status;
    }
    switch (test_state) {
        case STEPPER_TEST_STATE_IDLE:
        case STEPPER_TEST_STATE_COMPLETE:
            return STATUS_OK;
        case STEPPER_TEST_STATE_PREFLIGHT:
            run_preflight();
            test_state = STEPPER_TEST_STATE_SEND;
            return STATUS_OK;
        case STEPPER_TEST_STATE_SEND:
            step = &test_steps[step_index];
            status = bsp_stepper_response(STEPPER_TEST_MOTOR_ID, &response);
            response_sequence_before_send = status == STATUS_OK ? response.sequence : 0U;
            status = execute_step(step);
            if (status == STATUS_BUSY) {
                return STATUS_OK;
            }
            report_check(step->name, status, STATUS_OK);
            if (status != STATUS_OK) {
                test_state = STEPPER_TEST_STATE_ABORT_STOP;
                return STATUS_OK;
            }
            if (step->action == STEPPER_TEST_ACTION_ENABLE) {
                report_check("busy-buffer-guard",
                    bsp_stepper_enable(STEPPER_TEST_MOTOR_ID, true), STATUS_BUSY);
            }
            deadline_ms = now + STEPPER_TEST_RESPONSE_TIMEOUT_MS;
            test_state = STEPPER_TEST_STATE_WAIT_ACK;
            return STATUS_OK;
        case STEPPER_TEST_STATE_WAIT_ACK:
            step = &test_steps[step_index];
            status = bsp_stepper_response(STEPPER_TEST_MOTOR_ID, &response);
            if ((status == STATUS_OK) &&
                (response.sequence != response_sequence_before_send)) {
                if ((response.command == step->expected_command) &&
                    (response.code == STEPPER_TEST_RESPONSE_OK)) {
                    passed_checks++;
                    (void)bluetooth_service_printf(
                        "[stepper-test] PASS ack step=%s cmd=%02X code=%02X\r\n", step->name,
                        (unsigned int)response.command, (unsigned int)response.code);
                } else {
                    failed_checks++;
                    (void)bluetooth_service_printf(
                        "[stepper-test] FAIL ack step=%s cmd=%02X code=%02X\r\n", step->name,
                        (unsigned int)response.command, (unsigned int)response.code);
                    test_state = STEPPER_TEST_STATE_ABORT_STOP;
                    return STATUS_OK;
                }
                deadline_ms = now + step->settle_ms;
                test_state = STEPPER_TEST_STATE_WAIT_MOTION;
            } else if (deadline_reached(now, deadline_ms)) {
                failed_checks++;
                (void)bluetooth_service_printf("[stepper-test] FAIL ack-timeout step=%s\r\n",
                    step->name);
                test_state = STEPPER_TEST_STATE_ABORT_STOP;
            }
            return STATUS_OK;
        case STEPPER_TEST_STATE_WAIT_MOTION:
            if (!deadline_reached(now, deadline_ms)) {
                return STATUS_OK;
            }
            step = &test_steps[step_index];
            if (step->position_check != STEPPER_TEST_POSITION_CHECK_NONE) {
                test_state = STEPPER_TEST_STATE_SEND_POSITION_READ;
                return STATUS_OK;
            }
            step_index++;
            test_state = step_index < (sizeof(test_steps) / sizeof(test_steps[0]))
                             ? STEPPER_TEST_STATE_SEND
                             : STEPPER_TEST_STATE_POSTFLIGHT;
            return STATUS_OK;
        case STEPPER_TEST_STATE_SEND_POSITION_READ:
            status = bsp_stepper_position(STEPPER_TEST_MOTOR_ID, &position);
            position_sequence_before_read = status == STATUS_OK ? position.sequence : 0U;
            status = bsp_stepper_read_position(STEPPER_TEST_MOTOR_ID);
            if (status == STATUS_BUSY) {
                return STATUS_OK;
            }
            report_check("read-position", status, STATUS_OK);
            if (status != STATUS_OK) {
                test_state = STEPPER_TEST_STATE_ABORT_STOP;
                return STATUS_OK;
            }
            if (step_index == 1U) {
                report_check("position-busy-buffer-guard",
                    bsp_stepper_read_position(STEPPER_TEST_MOTOR_ID), STATUS_BUSY);
            }
            deadline_ms = now + STEPPER_TEST_POSITION_TIMEOUT_MS;
            test_state = STEPPER_TEST_STATE_WAIT_POSITION;
            return STATUS_OK;
        case STEPPER_TEST_STATE_WAIT_POSITION:
            step = &test_steps[step_index];
            status = bsp_stepper_position(STEPPER_TEST_MOTOR_ID, &position);
            if ((status == STATUS_OK) &&
                (position.sequence != position_sequence_before_read)) {
                position_error_deg = position.angle_deg - step->expected_position_deg;
                if (position_error_deg < 0.0F) {
                    position_error_deg = -position_error_deg;
                }
                if (step->position_check == STEPPER_TEST_POSITION_CHECK_EXACT) {
                    is_position_valid =
                        position_error_deg <= STEPPER_TEST_POSITION_TOLERANCE_DEG;
                } else {
                    is_position_valid =
                        (position.angle_deg >= -STEPPER_TEST_POSITION_TOLERANCE_DEG) &&
                        (position.angle_deg < step->expected_position_deg);
                }
                if (is_position_valid) {
                    passed_checks++;
                    if (step->position_check == STEPPER_TEST_POSITION_CHECK_EXACT) {
                        (void)bluetooth_service_printf(
                            "[stepper-test] PASS position step=%s target=%.2f actual=%.2f "
                            "error=%.2f\r\n",
                            step->name, (double)step->expected_position_deg,
                            (double)position.angle_deg, (double)position_error_deg);
                    } else {
                        (void)bluetooth_service_printf(
                            "[stepper-test] PASS active-stop actual=%.2f limit=%.2f\r\n",
                            (double)position.angle_deg, (double)step->expected_position_deg);
                    }
                } else {
                    failed_checks++;
                    if (step->position_check == STEPPER_TEST_POSITION_CHECK_EXACT) {
                        (void)bluetooth_service_printf(
                            "[stepper-test] FAIL position step=%s target=%.2f actual=%.2f "
                            "error=%.2f\r\n",
                            step->name, (double)step->expected_position_deg,
                            (double)position.angle_deg, (double)position_error_deg);
                    } else {
                        (void)bluetooth_service_printf(
                            "[stepper-test] FAIL active-stop actual=%.2f limit=%.2f\r\n",
                            (double)position.angle_deg, (double)step->expected_position_deg);
                    }
                    test_state = STEPPER_TEST_STATE_ABORT_STOP;
                    return STATUS_OK;
                }
                step_index++;
                test_state = STEPPER_TEST_STATE_SEND;
            } else if (deadline_reached(now, deadline_ms)) {
                failed_checks++;
                (void)bluetooth_service_printf(
                    "[stepper-test] FAIL position-timeout step=%s\r\n", step->name);
                test_state = STEPPER_TEST_STATE_ABORT_STOP;
            }
            return STATUS_OK;
        case STEPPER_TEST_STATE_POSTFLIGHT:
            report_check("move-after-disable",
                bsp_stepper_move(STEPPER_TEST_MOTOR_ID, 1.0F, STEPPER_TEST_SPEED_RPM_X10,
                    STEPPER_TEST_ACCELERATION_RPM_S, BSP_STEPPER_MODE_RELATIVE_CURRENT, false),
                STATUS_STATE_ERROR);
            (void)bluetooth_service_printf("[stepper-test] DONE pass=%u fail=%u\r\n",
                (unsigned int)passed_checks, (unsigned int)failed_checks);
            test_state = STEPPER_TEST_STATE_COMPLETE;
            return STATUS_OK;
        case STEPPER_TEST_STATE_ABORT_STOP:
            status = bsp_stepper_stop(STEPPER_TEST_MOTOR_ID);
            if (status == STATUS_BUSY) {
                return STATUS_OK;
            }
            report_check("abort-stop", status, STATUS_OK);
            deadline_ms = now + STEPPER_TEST_SHORT_SETTLE_MS;
            test_state = STEPPER_TEST_STATE_ABORT_DISABLE;
            return STATUS_OK;
        case STEPPER_TEST_STATE_ABORT_DISABLE:
            if (!deadline_reached(now, deadline_ms)) {
                return STATUS_OK;
            }
            status = bsp_stepper_enable(STEPPER_TEST_MOTOR_ID, false);
            if (status == STATUS_BUSY) {
                return STATUS_OK;
            }
            report_check("abort-disable", status, STATUS_OK);
            (void)bluetooth_service_printf("[stepper-test] ABORTED pass=%u fail=%u\r\n",
                (unsigned int)passed_checks, (unsigned int)failed_checks);
            test_state = STEPPER_TEST_STATE_COMPLETE;
            return STATUS_OK;
        default:
            test_state = STEPPER_TEST_STATE_ABORT_STOP;
            return STATUS_STATE_ERROR;
    }
}
