/**
 * @file driver_stepper.h
 * @brief ZDT X42S serial stepper driver.
 */
#ifndef USER_DRIVERS_DRIVER_STEPPER_H
#define USER_DRIVERS_DRIVER_STEPPER_H

#include "main.h"
#include "status.h"

typedef struct {
    UART_HandleTypeDef *uart;
    uint8_t id;
} driver_stepper_config_t;

typedef struct {
    driver_stepper_config_t config;
    uint8_t tx_buffer[16];
    volatile uint8_t busy;
    uint8_t enabled;
    uint8_t initialized;
} driver_stepper_t;

status_code_t driver_stepper_init(driver_stepper_t *stepper,
                                  const driver_stepper_config_t *config);
status_code_t driver_stepper_enable(driver_stepper_t *stepper, uint8_t enable);
status_code_t driver_stepper_move(driver_stepper_t *stepper, int32_t pulses,
                                  uint16_t speed, uint8_t absolute);
void driver_stepper_tx_complete_isr(driver_stepper_t *stepper, UART_HandleTypeDef *uart);

#endif
