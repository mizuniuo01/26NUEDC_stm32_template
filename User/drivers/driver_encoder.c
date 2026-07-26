/**
 * @file driver_encoder.c
 * @brief Timer quadrature encoder driver implementation.
 */
#include "driver_encoder.h"

status_code_t driver_encoder_init(driver_encoder_t *encoder,
                                  const driver_encoder_config_t *config)
{
    if ((encoder == NULL) || (config == NULL) || (config->timer == NULL) ||
        (config->sign == 0) || (config->counter_bits == 0U) ||
        (config->counter_bits > 32U)) {
        return STATUS_INVALID_ARGUMENT;
    }
    encoder->config = *config;
    encoder->last_count = 0U;
    encoder->delta = 0;
    encoder->initialized = 1U;
    __HAL_TIM_SET_COUNTER(config->timer, 0U);
    return HAL_TIM_Encoder_Start(config->timer, TIM_CHANNEL_ALL) == HAL_OK ?
           STATUS_OK : STATUS_IO_ERROR;
}

status_code_t driver_encoder_process(driver_encoder_t *encoder)
{
    uint32_t count;
    int32_t difference;
    uint32_t mask;

    if ((encoder == NULL) || (encoder->initialized == 0U)) {
        return STATUS_NOT_INITIALIZED;
    }
    count = __HAL_TIM_GET_COUNTER(encoder->config.timer);
    mask = encoder->config.counter_bits >= 32U ? UINT32_MAX :
           ((1UL << encoder->config.counter_bits) - 1UL);
    difference = (int32_t)((count - encoder->last_count) & mask);
    if (encoder->config.counter_bits < 32U) {
        uint32_t half = 1UL << (encoder->config.counter_bits - 1U);
        if ((uint32_t)difference >= half) {
            difference -= (int32_t)(1UL << encoder->config.counter_bits);
        }
    }
    encoder->last_count = count;
    encoder->delta = difference * encoder->config.sign;
    return STATUS_OK;
}

int32_t driver_encoder_delta(const driver_encoder_t *encoder)
{
    return (encoder == NULL) ? 0 : encoder->delta;
}
