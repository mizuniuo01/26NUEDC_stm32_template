/**
 * @file driver_ultrasonic.h
 * @brief Non-blocking HC-SR04 timer capture driver.
 */
#ifndef USER_DRIVERS_DRIVER_ULTRASONIC_H
#define USER_DRIVERS_DRIVER_ULTRASONIC_H

#include "main.h"
#include "status.h"

typedef struct {
    TIM_HandleTypeDef *timer;
    uint32_t channel;
    GPIO_TypeDef *trigger_port;
    uint16_t trigger_pin;
    uint32_t trigger_period_ms;
} driver_ultrasonic_config_t;

typedef struct {
    driver_ultrasonic_config_t config;
    volatile uint32_t rising_capture;
    volatile uint16_t distance_mm;
    volatile uint8_t waiting_fall;
    volatile uint8_t valid;
    uint32_t last_trigger_ms;
    uint8_t initialized;
} driver_ultrasonic_t;

status_code_t driver_ultrasonic_init(driver_ultrasonic_t *ultrasonic,
                                     const driver_ultrasonic_config_t *config);
status_code_t driver_ultrasonic_process(driver_ultrasonic_t *ultrasonic, uint32_t now_ms);
void driver_ultrasonic_capture_isr(driver_ultrasonic_t *ultrasonic,
                                   TIM_HandleTypeDef *timer, uint32_t channel);
status_code_t driver_ultrasonic_read(const driver_ultrasonic_t *ultrasonic,
                                     uint16_t *distance_mm, uint8_t *valid);

#endif
