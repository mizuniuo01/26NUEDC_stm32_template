/**
 * @file driver_stepper.c
 * @brief ZDT X42S serial stepper driver implementation.
 */
#include "driver_stepper.h"

static status_code_t send(driver_stepper_t *stepper, uint8_t length)
{
    if (stepper->busy != 0U) {
        return STATUS_BUSY;
    }
    stepper->busy = 1U;
    if (HAL_UART_Transmit_IT(stepper->config.uart, stepper->tx_buffer, length) != HAL_OK) {
        stepper->busy = 0U;
        return STATUS_IO_ERROR;
    }
    return STATUS_OK;
}

status_code_t driver_stepper_init(driver_stepper_t *stepper,
                                  const driver_stepper_config_t *config)
{
    if ((stepper == NULL) || (config == NULL) || (config->uart == NULL)) {
        return STATUS_INVALID_ARGUMENT;
    }
    stepper->config = *config;
    stepper->busy = 0U;
    stepper->enabled = 0U;
    stepper->initialized = 1U;
    return STATUS_OK;
}

status_code_t driver_stepper_enable(driver_stepper_t *stepper, uint8_t enable)
{
    if ((stepper == NULL) || (stepper->initialized == 0U)) {
        return STATUS_NOT_INITIALIZED;
    }
    stepper->enabled = enable != 0U ? 1U : 0U;
    return STATUS_OK;
}

status_code_t driver_stepper_move(driver_stepper_t *stepper, int32_t pulses,
                                  uint16_t speed, uint8_t absolute)
{
    uint32_t magnitude;

    if ((stepper == NULL) || (stepper->initialized == 0U)) {
        return STATUS_NOT_INITIALIZED;
    }
    if (stepper->enabled == 0U) {
        return STATUS_STATE_ERROR;
    }
    if (speed > 30000U) {
        speed = 30000U;
    }
    magnitude = pulses < 0 ? (uint32_t)(-(pulses + 1)) + 1U : (uint32_t)pulses;
    stepper->tx_buffer[0] = stepper->config.id;
    stepper->tx_buffer[1] = 0xFDU;
    stepper->tx_buffer[2] = pulses < 0 ? 0x01U : 0x00U;
    /* ZDT X42S FD frame: address, command, direction, acc, dec, speed,
     * big-endian position, mode, sync flag and fixed 0x6B checksum. */
    stepper->tx_buffer[3] = 0x01U;
    stepper->tx_buffer[4] = 0x2CU;
    stepper->tx_buffer[5] = 0x01U;
    stepper->tx_buffer[6] = 0x2CU;
    stepper->tx_buffer[7] = (uint8_t)(speed >> 8U);
    stepper->tx_buffer[8] = (uint8_t)speed;
    stepper->tx_buffer[9] = (uint8_t)(magnitude >> 24U);
    stepper->tx_buffer[10] = (uint8_t)(magnitude >> 16U);
    stepper->tx_buffer[11] = (uint8_t)(magnitude >> 8U);
    stepper->tx_buffer[12] = (uint8_t)magnitude;
    stepper->tx_buffer[13] = absolute != 0U ? 0x01U : 0x00U;
    stepper->tx_buffer[14] = 0x00U;
    stepper->tx_buffer[15] = 0x6BU;
    return send(stepper, 16U);
}

void driver_stepper_tx_complete_isr(driver_stepper_t *stepper, UART_HandleTypeDef *uart)
{
    if ((stepper != NULL) && (uart != NULL) && (stepper->initialized != 0U) &&
        (uart->Instance == stepper->config.uart->Instance)) {
        stepper->busy = 0U;
    }
}
