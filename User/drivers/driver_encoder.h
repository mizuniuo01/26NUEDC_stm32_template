/**
 * @file driver_encoder.h
 * @brief Timer quadrature encoder driver.
 */
#ifndef USER_DRIVERS_DRIVER_ENCODER_H
#define USER_DRIVERS_DRIVER_ENCODER_H

#include "main.h"
#include "status.h"

typedef struct {
    TIM_HandleTypeDef *timer;
    int8_t sign;
    uint8_t counter_bits;
} driver_encoder_config_t;

typedef struct {
    driver_encoder_config_t config;
    uint32_t last_count;
    int32_t delta;
    uint8_t initialized;
} driver_encoder_t;

status_code_t driver_encoder_init(driver_encoder_t *encoder,
                                  const driver_encoder_config_t *config);
status_code_t driver_encoder_process(driver_encoder_t *encoder);
int32_t driver_encoder_delta(const driver_encoder_t *encoder);

#endif
