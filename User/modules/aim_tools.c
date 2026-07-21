/**
 * @file    aim_tools.c
 * @brief   云台双轴闭环与扳机舵机工具模块。
 * @author  mizuniuo01
 * @date    2026-07-22
 * @version 2.0.0
 * @note    X/Y 使用参数相同但历史状态独立的 PID 实例。
 * @note    软件角度表示最近一次成功提交给舵机驱动的绝对目标角。
 * @note    射击等待通过 HAL tick 状态机完成，主循环不阻塞。
 */

#include "aim_tools.h"
#include "aim_config.h"
#include "pid.h"
#include "servo.h"
#include "system.h"

#include <math.h>
#include <stm32f4xx_hal.h>

/** @brief 云台闭环默认比例系数。 */
#define AIM_TOOLS_PID_KP 0.01F

/** @brief 云台闭环默认积分系数。 */
#define AIM_TOOLS_PID_KI 0.0F

/** @brief 云台闭环默认微分系数。 */
#define AIM_TOOLS_PID_KD 0.0F

/** @brief 每一视觉帧允许修改的最大舵机角度。 */
#define AIM_TOOLS_PID_OUTPUT_MAX_DEG 2.0F

/** @brief PID 积分项对输出的最大影响对应量。 */
#define AIM_TOOLS_PID_INTEGRAL_MAX 0.0F

/** @brief 目标偏差绝对值不超过该像素数时视为对应轴居中。 */
#define AIM_TOOLS_ERROR_DEADBAND_PX 3

/** @brief 扳机静止绝对角度。 */
#define AIM_TOOLS_TRIGGER_HOME_ANGLE_DEG 107.0F

/** @brief 扳机击发绝对角度。 */
#define AIM_TOOLS_TRIGGER_FIRE_ANGLE_DEG 17.0F

/** @brief 扳机每段动作的无反馈等待时间。 */
#define AIM_TOOLS_TRIGGER_PHASE_TIME_MS 150U

/** @brief 非阻塞射击状态。 */
typedef enum {
    AIM_TOOLS_SHOOT_STATE_IDLE = 0, /**< 扳机处于静止位置。 */
    AIM_TOOLS_SHOOT_STATE_WAIT_FIRE, /**< 等待扳机到达击发位置。 */
    AIM_TOOLS_SHOOT_STATE_WAIT_HOME, /**< 等待扳机返回静止位置。 */
} aim_tools_shoot_state_t;

/** @brief X 轴云台 PID。 */
static pid_t aim_tools_pid_x;

/** @brief Y 轴云台 PID。 */
static pid_t aim_tools_pid_y;

/** @brief 最近一次成功提交的 X 轴绝对目标角。 */
static float aim_tools_x_angle_deg;

/** @brief 最近一次成功提交的 Y 轴绝对目标角。 */
static float aim_tools_y_angle_deg;

/** @brief 当前射击状态。 */
static aim_tools_shoot_state_t aim_tools_shoot_state;

/** @brief 当前射击阶段开始时间。 */
static uint32_t aim_tools_shoot_phase_tick_ms;

/** @brief 模块初始化完成标志。 */
static uint8_t aim_tools_initialized;

/**
 * @brief  将舵机驱动状态转换为瞄准工具状态。
 * @param  servo_status  舵机驱动状态。
 * @return 对应瞄准工具状态。
 */
static aim_tools_status_t aim_tools_map_servo_status(servo_status_t servo_status)
{
    switch (servo_status) {
        case SERVO_STATUS_OK:
            return AIM_TOOLS_STATUS_OK;

        case SERVO_STATUS_INVALID_ARGUMENT:
            return AIM_TOOLS_STATUS_INVALID_ARGUMENT;

        case SERVO_STATUS_NOT_INITIALIZED:
            return AIM_TOOLS_STATUS_NOT_INITIALIZED;

        case SERVO_STATUS_QUEUE_FULL:
            return AIM_TOOLS_STATUS_QUEUE_FULL;

        default:
            return AIM_TOOLS_STATUS_IO_ERROR;
    }
}

/**
 * @brief  将角度限制到舵机允许的单圈范围。
 * @param  angle_deg  待限制角度。
 * @return 限制后的角度。
 */
static float aim_tools_clamp_angle(float angle_deg)
{
    if (angle_deg > SERVO_MAX_ANGLE_DEG) {
        return SERVO_MAX_ANGLE_DEG;
    }
    if (angle_deg < SERVO_MIN_ANGLE_DEG) {
        return SERVO_MIN_ANGLE_DEG;
    }
    return angle_deg;
}

/**
 * @brief  判断像素偏差是否位于闭环死区。
 * @param  error  有符号像素偏差。
 * @return 位于死区返回 1，否则返回 0。
 */
static uint8_t aim_tools_error_in_deadband(int16_t error)
{
    return ((error >= -AIM_TOOLS_ERROR_DEADBAND_PX) && (error <= AIM_TOOLS_ERROR_DEADBAND_PX)) ? 1U
                                                                                               : 0U;
}

/**
 * @brief  判断目标是否同时位于 X/Y 两轴闭环死区。
 * @param  error_x  目标 X 轴像素偏差。
 * @param  error_y  目标 Y 轴像素偏差。
 * @return 两轴偏差绝对值都不超过 3 像素时返回 1，否则返回 0。
 */
uint8_t aim_tools_target_is_centered(int16_t error_x, int16_t error_y)
{
    return aim_tools_error_in_deadband(error_x) && aim_tools_error_in_deadband(error_y);
}

/**
 * @brief  把 PID 输出映射为某轴舵机的角度增量。
 * @param  pid_output          以偏差 0 为目标得到的 PID 输出。
 * @param  positive_angle_moves_positive  舵机正角是否对应物理正方向。
 * @return 舵机绝对目标角应增加的角度。
 */
static float aim_tools_map_pid_output(float pid_output, uint8_t positive_angle_moves_positive)
{
    float physical_positive_delta = -pid_output;

    return positive_angle_moves_positive ? physical_positive_delta : -physical_positive_delta;
}

/**
 * @brief  向指定舵机提交绝对角度并同步软件命令角。
 * @param  servo       舵机实例。
 * @param  angle_deg   绝对目标角。
 * @param  saved_angle 软件命令角存储地址。
 * @return 舵机命令提交状态。
 */
static aim_tools_status_t aim_tools_set_axis_angle(servo_t *servo, float angle_deg,
    float *saved_angle)
{
    servo_status_t status;

    if ((servo == NULL) || (saved_angle == NULL) || !isfinite(angle_deg)) {
        return AIM_TOOLS_STATUS_INVALID_ARGUMENT;
    }

    angle_deg = aim_tools_clamp_angle(angle_deg);
    status = servo_set_angle(servo, angle_deg);
    if (status == SERVO_STATUS_OK) {
        *saved_angle = angle_deg;
    }
    return aim_tools_map_servo_status(status);
}

/**
 * @brief  根据一轴像素偏差计算并提交下一绝对目标角。
 * @param  pid                 对应轴 PID。
 * @param  error               当前像素偏差。
 * @param  positive_angle_moves_positive  舵机正角是否对应物理正方向。
 * @param  servo               对应舵机。
 * @param  saved_angle         当前软件命令角。
 * @return 该轴控制状态。
 */
static aim_tools_status_t aim_tools_track_axis(pid_t *pid, int16_t error,
    uint8_t positive_angle_moves_positive, servo_t *servo, float *saved_angle)
{
    float pid_output;
    float next_angle;

    if (aim_tools_error_in_deadband(error)) {
        pid_clear(pid);
        return AIM_TOOLS_STATUS_OK;
    }

    pid_output = pid_calc(pid, 0.0F, (float)error);
    next_angle = *saved_angle
                 + aim_tools_map_pid_output(pid_output, positive_angle_moves_positive);
    next_angle = aim_tools_clamp_angle(next_angle);
    if (next_angle == *saved_angle) {
        return AIM_TOOLS_STATUS_OK;
    }
    return aim_tools_set_axis_angle(servo, next_angle, saved_angle);
}

/**
 * @brief  初始化云台 PID、扳机静止角和云台 HOME 角。
 * @return 三个启动命令均成功入队返回 OK，否则返回首个错误。
 */
aim_tools_status_t aim_tools_init(void)
{
    aim_tools_status_t status;

    pid_init(&aim_tools_pid_x, AIM_TOOLS_PID_KP, AIM_TOOLS_PID_KI, AIM_TOOLS_PID_KD,
        AIM_TOOLS_PID_OUTPUT_MAX_DEG, AIM_TOOLS_PID_INTEGRAL_MAX);
    pid_init(&aim_tools_pid_y, AIM_TOOLS_PID_KP, AIM_TOOLS_PID_KI, AIM_TOOLS_PID_KD,
        AIM_TOOLS_PID_OUTPUT_MAX_DEG, AIM_TOOLS_PID_INTEGRAL_MAX);

    aim_tools_x_angle_deg = AIM_GIMBAL_X_HOME_ANGLE_DEG;
    aim_tools_y_angle_deg = AIM_GIMBAL_Y_HOME_ANGLE_DEG;
    aim_tools_shoot_state = AIM_TOOLS_SHOOT_STATE_IDLE;
    aim_tools_shoot_phase_tick_ms = HAL_GetTick();
    aim_tools_initialized = 1U;

    status = aim_tools_map_servo_status(servo_set_angle(system_servo_trigger(),
        AIM_TOOLS_TRIGGER_HOME_ANGLE_DEG));
    if (status != AIM_TOOLS_STATUS_OK) {
        return status;
    }
    status = aim_tools_set_axis_angle(system_servo_x_axis(), AIM_GIMBAL_X_HOME_ANGLE_DEG,
        &aim_tools_x_angle_deg);
    if (status != AIM_TOOLS_STATUS_OK) {
        return status;
    }
    return aim_tools_set_axis_angle(system_servo_y_axis(), AIM_GIMBAL_Y_HOME_ANGLE_DEG,
        &aim_tools_y_angle_deg);
}

/**
 * @brief  推进扳机非阻塞射击状态机。
 * @return 无。
 */
void aim_tools_task(void)
{
    uint32_t tick_ms;

    if (!aim_tools_initialized || (aim_tools_shoot_state == AIM_TOOLS_SHOOT_STATE_IDLE)) {
        return;
    }

    tick_ms = HAL_GetTick();
    if ((uint32_t)(tick_ms - aim_tools_shoot_phase_tick_ms) < AIM_TOOLS_TRIGGER_PHASE_TIME_MS) {
        return;
    }

    if (aim_tools_shoot_state == AIM_TOOLS_SHOOT_STATE_WAIT_FIRE) {
        if (servo_set_angle(system_servo_trigger(), AIM_TOOLS_TRIGGER_HOME_ANGLE_DEG) ==
            SERVO_STATUS_OK) {
            aim_tools_shoot_state = AIM_TOOLS_SHOOT_STATE_WAIT_HOME;
            aim_tools_shoot_phase_tick_ms = tick_ms;
        }
        return;
    }

    if (aim_tools_shoot_state == AIM_TOOLS_SHOOT_STATE_WAIT_HOME) {
        aim_tools_shoot_state = AIM_TOOLS_SHOOT_STATE_IDLE;
    }
}

/**
 * @brief  根据一帧 X/Y 像素偏差更新云台绝对目标角。
 * @param  error_x  目标在画面右侧时为正。
 * @param  error_y  目标在画面下方时为正。
 * @return 两轴均成功处理返回 OK，否则返回首个错误。
 */
aim_tools_status_t aim_tools_track_target(int16_t error_x, int16_t error_y)
{
    aim_tools_status_t status;

    if (!aim_tools_initialized) {
        return AIM_TOOLS_STATUS_NOT_INITIALIZED;
    }

    status = aim_tools_track_axis(&aim_tools_pid_x, error_x,
        AIM_X_POSITIVE_ANGLE_MOVES_RIGHT,
        system_servo_x_axis(), &aim_tools_x_angle_deg);
    if (status != AIM_TOOLS_STATUS_OK) {
        return status;
    }
    return aim_tools_track_axis(&aim_tools_pid_y, error_y,
        AIM_Y_POSITIVE_ANGLE_MOVES_DOWN,
        system_servo_y_axis(), &aim_tools_y_angle_deg);
}

/**
 * @brief  清除双轴 PID 并确保云台回到两个 HOME 角。
 * @return 命令提交状态。
 * @note   已成功提交 HOME 的轴不会在后续调用中重复发送。
 */
aim_tools_status_t aim_tools_return_home(void)
{
    aim_tools_status_t status = AIM_TOOLS_STATUS_OK;

    if (!aim_tools_initialized) {
        return AIM_TOOLS_STATUS_NOT_INITIALIZED;
    }

    pid_clear(&aim_tools_pid_x);
    pid_clear(&aim_tools_pid_y);
    if (aim_tools_x_angle_deg != AIM_GIMBAL_X_HOME_ANGLE_DEG) {
        status = aim_tools_set_axis_angle(system_servo_x_axis(), AIM_GIMBAL_X_HOME_ANGLE_DEG,
            &aim_tools_x_angle_deg);
    }
    if ((status == AIM_TOOLS_STATUS_OK) && (aim_tools_y_angle_deg != AIM_GIMBAL_Y_HOME_ANGLE_DEG)) {
        status = aim_tools_set_axis_angle(system_servo_y_axis(), AIM_GIMBAL_Y_HOME_ANGLE_DEG,
            &aim_tools_y_angle_deg);
    }
    return status;
}

/**
 * @brief  获取最近一次成功提交的双轴绝对目标角。
 * @param  x_deg  X 轴角度输出地址，允许为 NULL。
 * @param  y_deg  Y 轴角度输出地址，允许为 NULL。
 * @return 无。
 */
void aim_tools_get_gimbal_angles(float *x_deg, float *y_deg)
{
    if (x_deg != NULL) {
        *x_deg = aim_tools_x_angle_deg;
    }
    if (y_deg != NULL) {
        *y_deg = aim_tools_y_angle_deg;
    }
}

/**
 * @brief  启动一次非阻塞射击流程。
 * @return 已接受返回 OK，射击中返回 BUSY，队列错误返回对应状态。
 */
aim_tools_status_t aim_tools_shoot(void)
{
    servo_status_t status;

    if (!aim_tools_initialized) {
        return AIM_TOOLS_STATUS_NOT_INITIALIZED;
    }
    if (aim_tools_shoot_state != AIM_TOOLS_SHOOT_STATE_IDLE) {
        return AIM_TOOLS_STATUS_BUSY;
    }

    status = servo_set_angle(system_servo_trigger(), AIM_TOOLS_TRIGGER_FIRE_ANGLE_DEG);
    if (status != SERVO_STATUS_OK) {
        return aim_tools_map_servo_status(status);
    }

    aim_tools_shoot_state = AIM_TOOLS_SHOOT_STATE_WAIT_FIRE;
    aim_tools_shoot_phase_tick_ms = HAL_GetTick();
    return AIM_TOOLS_STATUS_OK;
}

/**
 * @brief  判断扳机射击流程是否尚未结束。
 * @return 射击中返回 1，空闲返回 0。
 */
uint8_t aim_tools_is_shooting(void)
{
    return (aim_tools_shoot_state == AIM_TOOLS_SHOOT_STATE_IDLE) ? 0U : 1U;
}
