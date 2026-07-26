/**
 * @file driver_servo.h
 * @brief FashionStar bus servo transport driver.
 */
#ifndef USER_DRIVERS_DRIVER_SERVO_H
#define USER_DRIVERS_DRIVER_SERVO_H

#include "main.h"
#include "status.h"

typedef struct {
    UART_HandleTypeDef *uart;
    uint8_t ids[3];
    uint8_t count;
    uint16_t interval_ms;
    uint16_t power;
} driver_servo_config_t;

typedef struct {
    driver_servo_config_t config;
    uint8_t tx_buffer[16];
    volatile uint8_t busy;
    uint8_t initialized;
} driver_servo_t;

status_code_t driver_servo_init(driver_servo_t *servo, const driver_servo_config_t *config);
status_code_t driver_servo_set_angle(driver_servo_t *servo, uint8_t id, float angle);
void driver_servo_tx_complete_isr(driver_servo_t *servo, UART_HandleTypeDef *uart);
void driver_servo_error_isr(driver_servo_t *servo, UART_HandleTypeDef *uart);

#endif
