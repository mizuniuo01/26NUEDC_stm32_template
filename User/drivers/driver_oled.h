/**
 * @file driver_oled.h
 * @brief SSD1306 I2C display driver with explicit refresh processing.
 */
#ifndef USER_DRIVERS_DRIVER_OLED_H
#define USER_DRIVERS_DRIVER_OLED_H

#include "main.h"
#include "status.h"

#define DRIVER_OLED_WIDTH 128U
#define DRIVER_OLED_HEIGHT 64U
#define DRIVER_OLED_BUFFER_SIZE (DRIVER_OLED_WIDTH * DRIVER_OLED_HEIGHT / 8U)

typedef struct {
    I2C_HandleTypeDef *i2c;
    uint16_t address;
} driver_oled_config_t;

typedef struct {
    driver_oled_config_t config;
    uint8_t buffer[DRIVER_OLED_BUFFER_SIZE];
    uint8_t tx_buffer[DRIVER_OLED_WIDTH + 1U];
    volatile uint8_t busy;
    volatile uint8_t refresh_requested;
    uint8_t page;
    uint8_t remaining_pages;
    uint8_t initialized;
} driver_oled_t;

status_code_t driver_oled_init(driver_oled_t *oled, const driver_oled_config_t *config);
void driver_oled_clear(driver_oled_t *oled);
status_code_t driver_oled_set_pixel(driver_oled_t *oled, uint8_t x, uint8_t y, uint8_t on);
status_code_t driver_oled_refresh(driver_oled_t *oled);
status_code_t driver_oled_process(driver_oled_t *oled);
void driver_oled_tx_complete_isr(driver_oled_t *oled, I2C_HandleTypeDef *i2c);
void driver_oled_error_isr(driver_oled_t *oled, I2C_HandleTypeDef *i2c);

#endif
