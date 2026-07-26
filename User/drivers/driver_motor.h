/**
 * @file driver_motor.h
 * @brief DRV8874 PH/EN dual motor driver.
 */
#ifndef USER_DRIVERS_DRIVER_MOTOR_H
#define USER_DRIVERS_DRIVER_MOTOR_H

#include "main.h"
#include "status.h"

typedef struct {
    TIM_HandleTypeDef *timer;
    GPIO_TypeDef *left_sleep_port;
    GPIO_TypeDef *right_sleep_port;
    GPIO_TypeDef *left_direction_port;
    GPIO_TypeDef *right_direction_port;
    uint16_t left_sleep_pin;
    uint16_t right_sleep_pin;
    uint16_t left_direction_pin;
    uint16_t right_direction_pin;
    uint32_t left_channel;
    uint32_t right_channel;
    uint16_t max_compare;
    uint16_t minimum_effective_compare;
} driver_motor_config_t;

typedef struct {
    driver_motor_config_t config;
    uint8_t initialized;
    uint8_t enabled;
} driver_motor_t;

status_code_t driver_motor_init(driver_motor_t *motor, const driver_motor_config_t *config);
status_code_t driver_motor_enable(driver_motor_t *motor);
status_code_t driver_motor_disable(driver_motor_t *motor);
status_code_t driver_motor_set(driver_motor_t *motor, int16_t left, int16_t right);

#endif
