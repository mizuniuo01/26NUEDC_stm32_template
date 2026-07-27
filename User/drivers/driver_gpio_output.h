#ifndef AUTO_BALL_CAR_USER_DRIVERS_DRIVER_GPIO_OUTPUT_H
#define AUTO_BALL_CAR_USER_DRIVERS_DRIVER_GPIO_OUTPUT_H /* 头文件保护 */

#include "status.h"
#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

#define DRIVER_GPIO_OUTPUT_CAPACITY 8U /* 单个输出组可容纳的 GPIO 数量 */

/* 一个具有语义有效电平的 GPIO 输出 */
typedef struct {
    GPIO_TypeDef *port;          /* GPIO 端口 */
    uint16_t pin;                /* HAL GPIO 引脚位掩码 */
    GPIO_PinState active_level;  /* 逻辑有效时写入的电平 */
} driver_gpio_output_pin_t;

/* 固定 GPIO 输出组实例 */
typedef struct {
    driver_gpio_output_pin_t pins[DRIVER_GPIO_OUTPUT_CAPACITY]; /* 输出配置副本 */
    uint8_t count;                                      /* 实际输出数量 */
    bool is_initialized;                                /* 驱动初始化完成标志 */
} driver_gpio_output_bank_t;

/* 生命周期与输出控制接口 */
status_code_t driver_gpio_output_init(driver_gpio_output_bank_t *bank,
    const driver_gpio_output_pin_t *pins, uint8_t count);
status_code_t driver_gpio_output_set(driver_gpio_output_bank_t *bank, uint8_t index,
    bool is_active);
status_code_t driver_gpio_output_set_mask(driver_gpio_output_bank_t *bank, uint8_t mask);

#endif /* AUTO_BALL_CAR_USER_DRIVERS_DRIVER_GPIO_OUTPUT_H */
