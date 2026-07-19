/**
 * @file    error_handler.c
 * @brief   统一错误处理模块（传输 + 上报 + 处理）
 * @author  mizuniuo01
 * @date    2026-05-27
 * @version 1.1.0
 * @note    依赖：display_show_error() 已由平台 display 模块提供
 * @note    错误恢复优先级（CODING_STANDARD.md §12.9.3）：
 *           - 模块有 ISR 错误回调（I2C/UART 外设中断）→ 在回调中闭环恢复
 *             （reset + re-enable + 回 IDLE），error_process 仅清队列
 *           - 模块无 ISR 错误回调 → error_process 的 case 中执行恢复
 * @note    error_process() 的 case 恢复动作需在具体项目中按硬件连接补充
 */

#include "error_handler.h"
#include "display.h"

/* 错误处理参数 */
typedef enum {
    ERROR_SOURCE_BIT_LIMIT = 32,
    ERROR_QUEUE_SIZE = ERROR_SOURCE_COUNT + 1,
    DRV_ERR_CODE_COUNT = 6,
} error_handler_param_t;

static drv_err_t error_states[ERROR_SOURCE_COUNT];
static uint32_t error_flags;
static error_source_t error_queue[ERROR_QUEUE_SIZE];
static uint8_t error_queue_head;
static uint8_t error_queue_tail;
static uint32_t error_queued_flags;

static const char *error_source_names[ERROR_SOURCE_COUNT] = {
    "Blueteeth",
    "Buzzer",
    "Cam",
    "Display",
    "Encoder",
    "Gyro",
    "Key",
    "Laser",
    "Led",
    "Motor",
    "Motor Left",
    "Motor Right",
    "Oled",
    "Pattern",
    "Pid",
    "Pwm",
    "Sensor",
    "Servo",
    "Step Motor",
    "Ultrasonic",
    "Menu",
};

static const char *error_code_names[DRV_ERR_CODE_COUNT] = {
    [0] = "OK",
    [-DRV_ERR_PARAM] = "PARAM",
    [-DRV_ERR_BUSY] = "BUSY",
    [-DRV_ERR_TIMEOUT] = "TIMEOUT",
    [-DRV_ERR_IO] = "IO",
    [-DRV_ERR_STATE] = "STATE",
};

/**
 * @brief  判断错误来源是否有效
 * @param  source  错误来源
 * @retval 0=无效，1=有效
 */
static uint8_t is_valid_source(error_source_t source)
{
    uint8_t s = (uint8_t)source;

    return (s < ERROR_SOURCE_COUNT) && (s < ERROR_SOURCE_BIT_LIMIT);
}

/**
 * @brief  获取错误来源对应的 bit mask
 * @param  source  错误来源
 * @retval 错误来源 bit mask
 */
static uint32_t error_source_mask(error_source_t source)
{
    return (uint32_t)1u << (uint32_t)source;
}

/**
 * @brief  查找第一个待上报错误来源
 * @param  无
 * @retval 错误来源，未找到时返回 ERROR_SOURCE_COUNT
 */
static error_source_t error_find_first_source(void)
{
    uint8_t i;

    for (i = 0; i < (uint8_t)ERROR_SOURCE_COUNT; i++) {
        if ((error_flags & ((uint32_t)1u << i)) != 0u) {
            return (error_source_t)i;
        }
    }

    return ERROR_SOURCE_COUNT;
}

/**
 * @brief  上报当前第一个错误到显示层
 * @param  无
 * @retval 无
 */
static void error_report_display(void)
{
    error_source_t source;

    if (error_flags == 0u) {
        display_show_error("");
        return;
    }

    source = error_find_first_source();
    if (!is_valid_source(source)) {
        return;
    }

    display_show_error("%s: %s", error_get_source_name(source),
        error_get_code_name(error_states[source]));
}

/**
 * @brief  处理一个排队错误
 * @param  无
 * @retval 无
 */
static void error_process(void)
{
    error_source_t source;

    if (error_queue_head == error_queue_tail) {
        return;
    }

    source = error_queue[error_queue_head];
    error_queue_head = (error_queue_head + 1u) % ERROR_QUEUE_SIZE;
    error_queued_flags &= ~error_source_mask(source);

    switch (source) {
        case ERROR_SOURCE_BLUETEETH:
        case ERROR_SOURCE_BUZZER:
        case ERROR_SOURCE_CAM:
        case ERROR_SOURCE_DISPLAY:
        case ERROR_SOURCE_ENCODER:
        case ERROR_SOURCE_GYRO:
        case ERROR_SOURCE_KEY:
        case ERROR_SOURCE_LASER:
        case ERROR_SOURCE_LED:
        case ERROR_SOURCE_MOTOR:
        case ERROR_SOURCE_MOTOR_LEFT:
        case ERROR_SOURCE_MOTOR_RIGHT:
        case ERROR_SOURCE_OLED:
        case ERROR_SOURCE_PATTERN:
        case ERROR_SOURCE_PID:
        case ERROR_SOURCE_PWM:
        case ERROR_SOURCE_SENSOR:
        case ERROR_SOURCE_SERVO:
        case ERROR_SOURCE_STEP_MOTOR:
        case ERROR_SOURCE_ULTRASONIC:
            break;
        default:
            break;
    }

    error_clear(source);
}

/**
 * @brief  上报错误
 * @param  source  错误来源
 * @param  code    错误码
 * @retval 无
 */
void error_report(error_source_t source, drv_err_t code)
{
    uint32_t mask;
    uint8_t next_tail;

    if (!is_valid_source(source)) {
        return;
    }

    if (code == DRV_OK) {
        error_clear(source);
        return;
    }

    mask = error_source_mask(source);
    error_states[source] = code;
    error_flags |= mask;

    if ((error_queued_flags & mask) == 0u) {
        next_tail = (error_queue_tail + 1u) % ERROR_QUEUE_SIZE;
        if (next_tail != error_queue_head) {
            error_queued_flags |= mask;
            error_queue[error_queue_tail] = source;
            error_queue_tail = next_tail;
        }
    }
}

/**
 * @brief  清除指定错误来源
 * @param  source  错误来源
 * @retval 无
 */
void error_clear(error_source_t source)
{
    if (!is_valid_source(source)) {
        return;
    }

    error_states[source] = DRV_OK;
    error_flags &= ~error_source_mask(source);
}

/**
 * @brief  错误处理任务
 * @param  无
 * @retval 无
 */
void error_handler_task(void)
{
    error_report_display();
    error_process();
}

/**
 * @brief  获取指定来源当前错误码
 * @param  source  错误来源
 * @retval 当前错误码，无效来源返回 DRV_OK
 */
drv_err_t error_get_state(error_source_t source)
{
    if (!is_valid_source(source)) {
        return DRV_OK;
    }

    return error_states[source];
}

/**
 * @brief  获取错误来源名称
 * @param  source  错误来源
 * @retval 错误来源名称
 */
const char *error_get_source_name(error_source_t source)
{
    if (!is_valid_source(source)) {
        return "Unknown";
    }

    return error_source_names[source];
}

/**
 * @brief  获取错误码名称
 * @param  code  错误码
 * @retval 错误码名称
 */
const char *error_get_code_name(drv_err_t code)
{
    int32_t index;

    index = -(int32_t)code;
    if ((index < 0) || (index >= DRV_ERR_CODE_COUNT) || (error_code_names[index] == 0)) {
        return "UNKNOWN";
    }

    return error_code_names[index];
}
