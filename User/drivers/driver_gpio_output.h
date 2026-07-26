/**
 * @file driver_gpio_output.h
 * @brief Fixed GPIO output bank driver.
 */
#ifndef USER_DRIVERS_DRIVER_GPIO_OUTPUT_H
#define USER_DRIVERS_DRIVER_GPIO_OUTPUT_H

#include "main.h"
#include "status.h"

#define DRIVER_GPIO_OUTPUT_MAX 8U

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    GPIO_PinState active_level;
} driver_gpio_output_pin_t;

typedef struct {
    driver_gpio_output_pin_t pins[DRIVER_GPIO_OUTPUT_MAX];
    uint8_t count;
    uint8_t initialized;
} driver_gpio_output_bank_t;

status_code_t driver_gpio_output_init(driver_gpio_output_bank_t *bank,
                                      const driver_gpio_output_pin_t *pins, uint8_t count);
status_code_t driver_gpio_output_set(driver_gpio_output_bank_t *bank, uint8_t index,
                                     uint8_t active);
status_code_t driver_gpio_output_set_mask(driver_gpio_output_bank_t *bank, uint8_t mask);

#endif
