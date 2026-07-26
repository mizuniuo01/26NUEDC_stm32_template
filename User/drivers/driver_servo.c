/**
 * @file driver_servo.c
 * @brief FashionStar bus servo transport driver implementation.
 */
#include "driver_servo.h"

status_code_t driver_servo_init(driver_servo_t *servo, const driver_servo_config_t *config)
{
    if ((servo == NULL) || (config == NULL) || (config->uart == NULL) ||
        (config->count == 0U) || (config->count > 3U)) {
        return STATUS_INVALID_ARGUMENT;
    }
    servo->config = *config;
    servo->busy = 0U;
    servo->initialized = 1U;
    return STATUS_OK;
}

status_code_t driver_servo_set_angle(driver_servo_t *servo, uint8_t id, float angle)
{
    int16_t angle_tenth;
    uint8_t checksum;
    uint8_t i;
    uint8_t known_id = 0U;

    if ((servo == NULL) || (servo->initialized == 0U)) {
        return STATUS_NOT_INITIALIZED;
    }
    if (servo->busy != 0U) {
        return STATUS_BUSY;
    }
    for (i = 0U; i < servo->config.count; i++) {
        if (servo->config.ids[i] == id) {
            known_id = 1U;
            break;
        }
    }
    if (known_id == 0U) {
        return STATUS_OUT_OF_RANGE;
    }
    if ((angle < -180.0f) || (angle > 180.0f)) {
        return STATUS_OUT_OF_RANGE;
    }
    angle_tenth = (int16_t)(angle * 10.0f);
    /* FashionStar FSUS: 0x4c12 header, ROTATE command, 7-byte payload. */
    servo->tx_buffer[0] = 0x12U;
    servo->tx_buffer[1] = 0x4CU;
    servo->tx_buffer[2] = 0x08U;
    servo->tx_buffer[3] = 0x07U;
    servo->tx_buffer[4] = id;
    servo->tx_buffer[5] = (uint8_t)angle_tenth;
    servo->tx_buffer[6] = (uint8_t)((uint16_t)angle_tenth >> 8U);
    servo->tx_buffer[7] = (uint8_t)servo->config.interval_ms;
    servo->tx_buffer[8] = (uint8_t)(servo->config.interval_ms >> 8U);
    servo->tx_buffer[9] = (uint8_t)servo->config.power;
    servo->tx_buffer[10] = (uint8_t)(servo->config.power >> 8U);
    /* FSUS uses an 8-bit additive checksum. */
    checksum = 0U;
    for (i = 0U; i < 11U; i++) {
        checksum = (uint8_t)(checksum + servo->tx_buffer[i]);
    }
    servo->tx_buffer[11] = checksum;
    servo->busy = 1U;
    if (HAL_UART_Transmit_IT(servo->config.uart, servo->tx_buffer, 12U) != HAL_OK) {
        servo->busy = 0U;
        return STATUS_IO_ERROR;
    }
    return STATUS_OK;
}

void driver_servo_tx_complete_isr(driver_servo_t *servo, UART_HandleTypeDef *uart)
{
    if ((servo != NULL) && (uart != NULL) && (servo->initialized != 0U) &&
        (uart->Instance == servo->config.uart->Instance)) {
        servo->busy = 0U;
    }
}

void driver_servo_error_isr(driver_servo_t *servo, UART_HandleTypeDef *uart)
{
    if ((servo != NULL) && (uart != NULL) && (servo->initialized != 0U) &&
        (uart->Instance == servo->config.uart->Instance)) {
        servo->busy = 0U;
    }
}
