#ifndef AUTO_BALL_CAR_USER_DRIVERS_DRIVER_SERVO_H
#define AUTO_BALL_CAR_USER_DRIVERS_DRIVER_SERVO_H /* 头文件保护 */

#include "status.h"
#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

/* FashionStar 总线舵机配置 */
typedef struct {
    UART_HandleTypeDef *uart; /* CubeMX 生成的串口句柄 */
    uint8_t ids[3];           /* 允许控制的舵机 ID 表 */
    uint8_t count;            /* ID 表中的有效舵机数量 */
    uint16_t interval_ms;     /* 默认动作时间，单位：毫秒 */
    uint16_t power;           /* 协议命令使用的功率参数 */
} driver_servo_config_t;

/* FashionStar 总线舵机驱动实例 */
/* 主循环设置 is_busy，串口回调清除；目标 MCU 已验证 bool 访问原子性。 */
typedef struct {
    driver_servo_config_t config; /* 配置快照 */
    uint8_t tx_buffer[16];        /* 单条协议命令发送缓冲区 */
    volatile bool is_busy;        /* UART DMA 发送进行中标志 */
    bool is_initialized;          /* 驱动初始化完成标志 */
} driver_servo_t;

/* 生命周期与舵机控制接口 */
status_code_t driver_servo_init(driver_servo_t *servo, const driver_servo_config_t *config);
status_code_t driver_servo_set_angle(driver_servo_t *servo, uint8_t id, float angle);

/* HAL 中断完成回调转发接口 */
void driver_servo_tx_complete_isr(driver_servo_t *servo, UART_HandleTypeDef *uart);
void driver_servo_error_isr(driver_servo_t *servo, UART_HandleTypeDef *uart);

#endif /* AUTO_BALL_CAR_USER_DRIVERS_DRIVER_SERVO_H */
