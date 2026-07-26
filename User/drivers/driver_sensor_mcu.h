/**
 * @file driver_sensor_mcu.h
 * @brief I2C DMA driver for the MCU version of the line sensor.
 */
#ifndef USER_DRIVERS_DRIVER_SENSOR_MCU_H
#define USER_DRIVERS_DRIVER_SENSOR_MCU_H

#include "main.h"
#include "status.h"

typedef struct {
    I2C_HandleTypeDef *i2c;
    uint16_t address;
    uint8_t command;
} driver_sensor_mcu_config_t;

typedef struct {
    driver_sensor_mcu_config_t config;
    volatile uint8_t raw;
    volatile uint8_t busy;
    volatile uint8_t valid;
    volatile uint32_t sequence;
    volatile uint32_t timestamp_ms;
    uint8_t initialized;
} driver_sensor_mcu_t;

status_code_t driver_sensor_mcu_init(driver_sensor_mcu_t *sensor,
                                     const driver_sensor_mcu_config_t *config);
status_code_t driver_sensor_mcu_request(driver_sensor_mcu_t *sensor);
void driver_sensor_mcu_rx_complete_isr(driver_sensor_mcu_t *sensor,
                                        I2C_HandleTypeDef *i2c, uint32_t timestamp_ms);
void driver_sensor_mcu_error_isr(driver_sensor_mcu_t *sensor, I2C_HandleTypeDef *i2c);
status_code_t driver_sensor_mcu_snapshot(const driver_sensor_mcu_t *sensor,
                                         uint8_t *value, uint8_t *valid,
                                         uint32_t *sequence, uint32_t *timestamp_ms);

#endif
