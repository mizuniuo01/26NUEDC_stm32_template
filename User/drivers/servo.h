#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>
#include <stm32f4xx_hal.h>

/* 舵机驱动接口状态。 */
typedef enum {
    SERVO_STATUS_OK = 0,                /* 操作成功。 */
    SERVO_STATUS_INVALID_ARGUMENT = -1, /* 参数、地址或角度无效。 */
    SERVO_STATUS_NOT_INITIALIZED = -2,  /* 总线或舵机尚未初始化。 */
    SERVO_STATUS_QUEUE_FULL = -3,       /* 非阻塞发送队列已满。 */
} servo_status_t;

/* 本项目三个舵机的总线地址。 */
typedef enum {
    SERVO_ADDRESS_TRIGGER = 0U, /* 扳机舵机。 */
    SERVO_ADDRESS_X_AXIS = 1U,  /* 云台 X 轴舵机。 */
    SERVO_ADDRESS_Y_AXIS = 12U, /* 云台 Y 轴舵机。 */
} servo_address_t;

/* 本项目统一使用的单圈绝对角度范围。 */
#define SERVO_MIN_ANGLE_DEG (-135.0F)
#define SERVO_MAX_ANGLE_DEG 135.0F

/* 单个舵机实例；由 system 持有，调用方不得直接修改成员。 */
typedef struct {
    uint8_t address;        /* 舵机总线地址。 */
    uint8_t is_initialized; /* 实例初始化完成标志。 */
} servo_t;

/* 初始化三个舵机共用的 UART 总线，不向舵机发送任何命令。 */
servo_status_t servo_bus_init(UART_HandleTypeDef *huart);

/* 在共享总线上注册一个舵机实例，不改变舵机当前角度。 */
servo_status_t servo_init(servo_t *servo, servo_address_t address);

/* 非阻塞设置指定舵机的单圈绝对角度。 */
servo_status_t servo_set_angle(servo_t *servo, float angle_deg);

/* 在主循环中反复调用，推进 UART 中断发送队列。 */
void servo_task(void);

/* 从 UART4 HAL 回调转发发送完成和错误事件。 */
void servo_tx_callback(UART_HandleTypeDef *huart);
void servo_error_callback(UART_HandleTypeDef *huart);

#endif /* SERVO_H */
