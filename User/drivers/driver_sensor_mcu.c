/**
 * @file driver_sensor_mcu.c
 * @brief I2C DMA driver for the MCU line sensor implementation.
 */
#include "driver_sensor_mcu.h"

static uint8_t receive_byte;

status_code_t driver_sensor_mcu_init(driver_sensor_mcu_t *sensor,
                                     const driver_sensor_mcu_config_t *config)
{
    if ((sensor == NULL) || (config == NULL) || (config->i2c == NULL)) {
        return STATUS_INVALID_ARGUMENT;
    }
    sensor->config = *config;
    sensor->raw = 0U;
    sensor->busy = 0U;
    sensor->valid = 0U;
    sensor->sequence = 0U;
    sensor->timestamp_ms = 0U;
    sensor->initialized = 1U;
    return STATUS_OK;
}

status_code_t driver_sensor_mcu_request(driver_sensor_mcu_t *sensor)
{
    HAL_StatusTypeDef result;

    if ((sensor == NULL) || (sensor->initialized == 0U)) {
        return STATUS_NOT_INITIALIZED;
    }
    if (sensor->busy != 0U) {
        return STATUS_BUSY;
    }
    sensor->busy = 1U;
    result = HAL_I2C_Mem_Read_DMA(sensor->config.i2c, sensor->config.address,
                                  sensor->config.command, I2C_MEMADD_SIZE_8BIT,
                                  &receive_byte, 1U);
    if (result != HAL_OK) {
        sensor->busy = 0U;
        return result == HAL_BUSY ? STATUS_BUSY :
               (result == HAL_TIMEOUT ? STATUS_TIMEOUT : STATUS_IO_ERROR);
    }
    return STATUS_OK;
}

void driver_sensor_mcu_rx_complete_isr(driver_sensor_mcu_t *sensor,
                                        I2C_HandleTypeDef *i2c, uint32_t timestamp_ms)
{
    if ((sensor == NULL) || (i2c == NULL) || (sensor->initialized == 0U) ||
        (i2c->Instance != sensor->config.i2c->Instance)) {
        return;
    }
    sensor->raw = (uint8_t)~receive_byte;
    sensor->busy = 0U;
    sensor->valid = 1U;
    sensor->sequence++;
    sensor->timestamp_ms = timestamp_ms;
}

void driver_sensor_mcu_error_isr(driver_sensor_mcu_t *sensor, I2C_HandleTypeDef *i2c)
{
    if ((sensor != NULL) && (i2c != NULL) && (sensor->initialized != 0U) &&
        (i2c->Instance == sensor->config.i2c->Instance)) {
        sensor->busy = 0U;
        sensor->valid = 0U;
    }
}

status_code_t driver_sensor_mcu_snapshot(const driver_sensor_mcu_t *sensor,
                                         uint8_t *value, uint8_t *valid,
                                         uint32_t *sequence, uint32_t *timestamp_ms)
{
    if ((sensor == NULL) || (sensor->initialized == 0U)) {
        return STATUS_NOT_INITIALIZED;
    }
    if (value != NULL) {
        *value = sensor->raw;
    }
    if (valid != NULL) {
        *valid = sensor->valid;
    }
    if (sequence != NULL) {
        *sequence = sensor->sequence;
    }
    if (timestamp_ms != NULL) {
        *timestamp_ms = sensor->timestamp_ms;
    }
    return STATUS_OK;
}
