#ifndef AUTO_BALL_CAR_USER_DRIVERS_DRIVER_MOTOR_H
#define AUTO_BALL_CAR_USER_DRIVERS_DRIVER_MOTOR_H /* 头文件保护 */

#include "status.h"
#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

/* DRV8874 双路 PH/EN 电机驱动配置 */
typedef struct {
    TIM_HandleTypeDef *timer;             /* PWM 定时器句柄 */
    GPIO_TypeDef *left_sleep_port;        /* 左电机休眠控制端口 */
    GPIO_TypeDef *right_sleep_port;       /* 右电机休眠控制端口 */
    GPIO_TypeDef *left_direction_port;    /* 左电机方向控制端口 */
    GPIO_TypeDef *right_direction_port;   /* 右电机方向控制端口 */
    uint16_t left_sleep_pin;              /* 左电机休眠控制引脚 */
    uint16_t right_sleep_pin;             /* 右电机休眠控制引脚 */
    uint16_t left_direction_pin;          /* 左电机方向控制引脚 */
    uint16_t right_direction_pin;         /* 右电机方向控制引脚 */
    uint32_t left_channel;                /* 左电机 PWM 定时器通道 */
    uint32_t right_channel;               /* 右电机 PWM 定时器通道 */
    uint16_t max_compare;                 /* PWM 最大比较值 */
    uint16_t minimum_effective_compare;   /* 非零输出使用的最小有效比较值 */
} driver_motor_config_t;

/* DRV8874 双路电机驱动实例 */
typedef struct {
    driver_motor_config_t config; /* 配置快照 */
    bool is_initialized;          /* 驱动初始化完成标志 */
    bool is_enabled;              /* 电机输出使能标志 */
} driver_motor_t;

/* 生命周期与电机输出控制接口 */
status_code_t driver_motor_init(driver_motor_t *motor, const driver_motor_config_t *config);
status_code_t driver_motor_enable(driver_motor_t *motor);
status_code_t driver_motor_disable(driver_motor_t *motor);
status_code_t driver_motor_set(driver_motor_t *motor, int16_t left, int16_t right);

#endif /* AUTO_BALL_CAR_USER_DRIVERS_DRIVER_MOTOR_H */
