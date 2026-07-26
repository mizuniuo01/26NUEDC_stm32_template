/**
 * @file driver_motor.c
 * @brief DRV8874 PH/EN dual motor driver implementation.
 */
#include "driver_motor.h"

#include <stdlib.h>

static uint16_t clamp_compare(int16_t value, uint16_t maximum, uint16_t minimum)
{
    uint16_t compare = (uint16_t)abs(value);

    if (compare > maximum) {
        compare = maximum;
    }
    if (compare < minimum) {
        compare = 0U;
    }
    return compare;
}

static void write_motor(driver_motor_t *motor, uint8_t right, int16_t value)
{
    GPIO_TypeDef *direction_port = right ? motor->config.right_direction_port :
                                           motor->config.left_direction_port;
    uint16_t direction_pin = right ? motor->config.right_direction_pin :
                                     motor->config.left_direction_pin;
    uint32_t channel = right ? motor->config.right_channel : motor->config.left_channel;
    GPIO_PinState forward_level;
    uint16_t compare = clamp_compare(value, motor->config.max_compare,
                                     motor->config.minimum_effective_compare);

    /* Preserve the board's mechanical direction convention. */
    forward_level = right ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(direction_port, direction_pin,
                      value >= 0 ? forward_level :
                                   (forward_level == GPIO_PIN_SET ? GPIO_PIN_RESET : GPIO_PIN_SET));
    __HAL_TIM_SET_COMPARE(motor->config.timer, channel, compare);
}

status_code_t driver_motor_init(driver_motor_t *motor, const driver_motor_config_t *config)
{
    if ((motor == NULL) || (config == NULL) || (config->timer == NULL)) {
        return STATUS_INVALID_ARGUMENT;
    }
    motor->config = *config;
    motor->initialized = 1U;
    motor->enabled = 0U;
    HAL_GPIO_WritePin(config->left_sleep_port, config->left_sleep_pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(config->right_sleep_port, config->right_sleep_pin, GPIO_PIN_RESET);
    __HAL_TIM_SET_COMPARE(config->timer, config->left_channel, 0U);
    __HAL_TIM_SET_COMPARE(config->timer, config->right_channel, 0U);
    if (HAL_TIM_PWM_Start(config->timer, config->left_channel) != HAL_OK) {
        return STATUS_IO_ERROR;
    }
    if (HAL_TIM_PWM_Start(config->timer, config->right_channel) != HAL_OK) {
        return STATUS_IO_ERROR;
    }
    return STATUS_OK;
}

status_code_t driver_motor_enable(driver_motor_t *motor)
{
    if ((motor == NULL) || (motor->initialized == 0U)) {
        return STATUS_NOT_INITIALIZED;
    }
    HAL_GPIO_WritePin(motor->config.left_sleep_port, motor->config.left_sleep_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(motor->config.right_sleep_port, motor->config.right_sleep_pin, GPIO_PIN_SET);
    motor->enabled = 1U;
    return STATUS_OK;
}

status_code_t driver_motor_disable(driver_motor_t *motor)
{
    if ((motor == NULL) || (motor->initialized == 0U)) {
        return STATUS_NOT_INITIALIZED;
    }
    __HAL_TIM_SET_COMPARE(motor->config.timer, motor->config.left_channel, 0U);
    __HAL_TIM_SET_COMPARE(motor->config.timer, motor->config.right_channel, 0U);
    HAL_GPIO_WritePin(motor->config.left_sleep_port, motor->config.left_sleep_pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(motor->config.right_sleep_port, motor->config.right_sleep_pin,
                      GPIO_PIN_RESET);
    motor->enabled = 0U;
    return STATUS_OK;
}

status_code_t driver_motor_set(driver_motor_t *motor, int16_t left, int16_t right)
{
    if ((motor == NULL) || (motor->initialized == 0U)) {
        return STATUS_NOT_INITIALIZED;
    }
    if (motor->enabled == 0U) {
        return STATUS_STATE_ERROR;
    }
    write_motor(motor, 0U, left);
    write_motor(motor, 1U, right);
    return STATUS_OK;
}
