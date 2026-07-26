/**
 * @file driver_ultrasonic.c
 * @brief Non-blocking HC-SR04 timer capture driver implementation.
 */
#include "driver_ultrasonic.h"

status_code_t driver_ultrasonic_init(driver_ultrasonic_t *ultrasonic,
                                     const driver_ultrasonic_config_t *config)
{
    if ((ultrasonic == NULL) || (config == NULL) || (config->timer == NULL) ||
        (config->trigger_port == NULL)) {
        return STATUS_INVALID_ARGUMENT;
    }
    ultrasonic->config = *config;
    ultrasonic->rising_capture = 0U;
    ultrasonic->distance_mm = 0U;
    ultrasonic->waiting_fall = 0U;
    ultrasonic->valid = 0U;
    ultrasonic->last_trigger_ms = 0U;
    ultrasonic->initialized = 1U;
    HAL_GPIO_WritePin(config->trigger_port, config->trigger_pin, GPIO_PIN_RESET);
    return HAL_TIM_IC_Start_IT(config->timer, config->channel) == HAL_OK ?
           STATUS_OK : STATUS_IO_ERROR;
}

status_code_t driver_ultrasonic_process(driver_ultrasonic_t *ultrasonic, uint32_t now_ms)
{
    if ((ultrasonic == NULL) || (ultrasonic->initialized == 0U)) {
        return STATUS_NOT_INITIALIZED;
    }
    if ((uint32_t)(now_ms - ultrasonic->last_trigger_ms) >=
        ultrasonic->config.trigger_period_ms) {
        ultrasonic->last_trigger_ms = now_ms;
        HAL_GPIO_WritePin(ultrasonic->config.trigger_port,
                          ultrasonic->config.trigger_pin, GPIO_PIN_SET);
        /* The trigger pin is returned low on the following process call. */
    } else {
        HAL_GPIO_WritePin(ultrasonic->config.trigger_port,
                          ultrasonic->config.trigger_pin, GPIO_PIN_RESET);
    }
    return STATUS_OK;
}

void driver_ultrasonic_capture_isr(driver_ultrasonic_t *ultrasonic,
                                   TIM_HandleTypeDef *timer, uint32_t channel)
{
    uint32_t capture;
    uint32_t width;

    if ((ultrasonic == NULL) || (timer == NULL) || (timer != ultrasonic->config.timer) ||
        (channel != ultrasonic->config.channel) || (ultrasonic->initialized == 0U)) {
        return;
    }
    capture = HAL_TIM_ReadCapturedValue(timer, channel);
    if (ultrasonic->waiting_fall == 0U) {
        ultrasonic->rising_capture = capture;
        ultrasonic->waiting_fall = 1U;
        __HAL_TIM_SET_CAPTUREPOLARITY(timer, channel, TIM_INPUTCHANNELPOLARITY_FALLING);
    } else {
        width = (capture >= ultrasonic->rising_capture) ?
                (capture - ultrasonic->rising_capture) :
                (0x10000UL - ultrasonic->rising_capture + capture);
        ultrasonic->distance_mm = (uint16_t)((width * 10UL) / 58UL);
        ultrasonic->valid = 1U;
        ultrasonic->waiting_fall = 0U;
        __HAL_TIM_SET_CAPTUREPOLARITY(timer, channel, TIM_INPUTCHANNELPOLARITY_RISING);
    }
}

status_code_t driver_ultrasonic_read(const driver_ultrasonic_t *ultrasonic,
                                     uint16_t *distance_mm, uint8_t *valid)
{
    if ((ultrasonic == NULL) || (ultrasonic->initialized == 0U)) {
        return STATUS_NOT_INITIALIZED;
    }
    if (distance_mm != NULL) {
        *distance_mm = ultrasonic->distance_mm;
    }
    if (valid != NULL) {
        *valid = ultrasonic->valid;
    }
    return STATUS_OK;
}
