#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

#include <stdint.h>

/* 错误码 */
typedef enum {
    DRV_OK = 0,
    DRV_ERR_PARAM = -1,     /* 参数非法（含空指针、越界） */
    DRV_ERR_BUSY = -2,      /* 设备忙 */
    DRV_ERR_TIMEOUT = -3,   /* 操作超时 */
    DRV_ERR_IO = -4,        /* 硬件通信错误 */
    DRV_ERR_STATE = -5,     /* 状态机状态非法 */
} drv_err_t;

/* 错误来源 */
typedef enum {
    ERROR_SOURCE_BLUETEETH = 0,
    ERROR_SOURCE_BUZZER,
    ERROR_SOURCE_CAM,
    ERROR_SOURCE_DISPLAY,
    ERROR_SOURCE_ENCODER,
    ERROR_SOURCE_GYRO,
    ERROR_SOURCE_KEY,
    ERROR_SOURCE_LASER,
    ERROR_SOURCE_LED,
    ERROR_SOURCE_MOTOR,
    ERROR_SOURCE_MOTOR_LEFT,
    ERROR_SOURCE_MOTOR_RIGHT,
    ERROR_SOURCE_OLED,
    ERROR_SOURCE_PATTERN,
    ERROR_SOURCE_PID,
    ERROR_SOURCE_PWM,
    ERROR_SOURCE_SENSOR,
    ERROR_SOURCE_SERVO,
    ERROR_SOURCE_STEP_MOTOR,
    ERROR_SOURCE_ULTRASONIC,
    ERROR_SOURCE_MENU,
    ERROR_SOURCE_COUNT,
} error_source_t;

void error_report(error_source_t source, drv_err_t code);
void error_clear(error_source_t source);
void error_handler_task(void);
drv_err_t error_get_state(error_source_t source);
const char *error_get_source_name(error_source_t source);
const char *error_get_code_name(drv_err_t code);

#endif
