#ifndef AUTO_BALL_CAR_USER_DRIVERS_DRIVER_STEPPER_H
#define AUTO_BALL_CAR_USER_DRIVERS_DRIVER_STEPPER_H /* 头文件保护 */

#include "status.h"
#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

/* ZDT X42S FD 指令的位置模式 */
typedef enum {
    DRIVER_STEPPER_MODE_RELATIVE_TARGET = 0x00U, /* 相对上一目标位置 */
    DRIVER_STEPPER_MODE_ABSOLUTE = 0x01U,        /* 相对坐标零点 */
    DRIVER_STEPPER_MODE_RELATIVE_CURRENT = 0x02U /* 相对当前实时位置 */
} driver_stepper_move_mode_t;

/* ZDT X42S 串口步进电机配置 */
typedef struct {
    UART_HandleTypeDef *uart; /* CubeMX 生成的串口句柄 */
    uint8_t id;               /* 步进电机总线 ID */
} driver_stepper_config_t;

/* ZDT X42S 串口步进电机驱动实例 */
/* 主循环设置 is_busy，串口回调清除；目标 MCU 已验证 bool 访问原子性。 */
typedef struct {
    driver_stepper_config_t config; /* 配置快照 */
    uint8_t tx_buffer[16];          /* 单条协议命令发送缓冲区 */
    volatile bool is_busy;          /* UART DMA 发送进行中标志 */
    bool is_enabled;                /* 步进电机使能标志 */
    bool is_initialized;            /* 驱动初始化完成标志 */
} driver_stepper_t;

/* 生命周期与步进电机控制接口 */
status_code_t driver_stepper_init(driver_stepper_t *stepper, const driver_stepper_config_t *config);
status_code_t driver_stepper_enable(driver_stepper_t *stepper, bool is_enabled);
status_code_t driver_stepper_move(driver_stepper_t *stepper, float angle, uint16_t speed,
    uint16_t acceleration, driver_stepper_move_mode_t mode, bool is_synchronized);

/* HAL 中断完成回调转发接口 */
void driver_stepper_tx_complete_isr(driver_stepper_t *stepper, UART_HandleTypeDef *uart);
void driver_stepper_error_isr(driver_stepper_t *stepper, UART_HandleTypeDef *uart);

#endif /* AUTO_BALL_CAR_USER_DRIVERS_DRIVER_STEPPER_H */
