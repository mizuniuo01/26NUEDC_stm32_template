/**
 * @file driver_oled.c
 * @brief SSD1306 I2C display driver implementation.
 */
#include "driver_oled.h"

#include <string.h>

static status_code_t send_command(driver_oled_t *oled, uint8_t command)
{
    uint8_t packet[2] = {0x00U, command};
    return HAL_I2C_Master_Transmit(oled->config.i2c, oled->config.address,
                                   packet, sizeof(packet), 20U) == HAL_OK ?
           STATUS_OK : STATUS_IO_ERROR;
}

status_code_t driver_oled_init(driver_oled_t *oled, const driver_oled_config_t *config)
{
    static const uint8_t init_sequence[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14,
        0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0x7F, 0xD9, 0xF1,
        0xDB, 0x40, 0xA4, 0xA6, 0xAF
    };
    uint8_t i;

    if ((oled == NULL) || (config == NULL) || (config->i2c == NULL)) {
        return STATUS_INVALID_ARGUMENT;
    }
    oled->config = *config;
    oled->busy = 0U;
    oled->refresh_requested = 0U;
    oled->page = 0U;
    oled->remaining_pages = 0U;
    oled->initialized = 1U;
    driver_oled_clear(oled);
    for (i = 0U; i < sizeof(init_sequence); i++) {
        if (send_command(oled, init_sequence[i]) != STATUS_OK) {
            oled->initialized = 0U;
            return STATUS_IO_ERROR;
        }
    }
    return STATUS_OK;
}

void driver_oled_clear(driver_oled_t *oled)
{
    if (oled != NULL) {
        (void)memset(oled->buffer, 0, sizeof(oled->buffer));
    }
}

status_code_t driver_oled_set_pixel(driver_oled_t *oled, uint8_t x, uint8_t y, uint8_t on)
{
    uint16_t index;
    uint8_t mask;

    if ((oled == NULL) || (oled->initialized == 0U)) {
        return STATUS_NOT_INITIALIZED;
    }
    if ((x >= DRIVER_OLED_WIDTH) || (y >= DRIVER_OLED_HEIGHT)) {
        return STATUS_OUT_OF_RANGE;
    }
    index = (uint16_t)x + (uint16_t)(y / 8U) * DRIVER_OLED_WIDTH;
    mask = (uint8_t)(1U << (y % 8U));
    if (on != 0U) {
        oled->buffer[index] |= mask;
    } else {
        oled->buffer[index] &= (uint8_t)~mask;
    }
    return STATUS_OK;
}

status_code_t driver_oled_refresh(driver_oled_t *oled)
{
    if ((oled == NULL) || (oled->initialized == 0U)) {
        return STATUS_NOT_INITIALIZED;
    }
    if ((oled->busy != 0U) || (oled->remaining_pages != 0U)) {
        return STATUS_BUSY;
    }
    oled->page = 0U;
    oled->remaining_pages = 8U;
    oled->refresh_requested = 1U;
    return STATUS_OK;
}

status_code_t driver_oled_process(driver_oled_t *oled)
{
    uint16_t offset;
    HAL_StatusTypeDef result;

    if ((oled == NULL) || (oled->initialized == 0U)) {
        return STATUS_NOT_INITIALIZED;
    }
    if ((oled->busy != 0U) || (oled->refresh_requested == 0U)) {
        return STATUS_OK;
    }
    offset = (uint16_t)oled->page * DRIVER_OLED_WIDTH;
    oled->tx_buffer[0] = 0x40U;
    (void)memcpy(&oled->tx_buffer[1], &oled->buffer[offset], DRIVER_OLED_WIDTH);
    oled->busy = 1U;
    result = HAL_I2C_Master_Transmit_IT(oled->config.i2c, oled->config.address,
                                        oled->tx_buffer, sizeof(oled->tx_buffer));
    if (result != HAL_OK) {
        oled->busy = 0U;
        return result == HAL_BUSY ? STATUS_BUSY : STATUS_IO_ERROR;
    }
    oled->refresh_requested = 0U;
    return STATUS_OK;
}

void driver_oled_tx_complete_isr(driver_oled_t *oled, I2C_HandleTypeDef *i2c)
{
    if ((oled == NULL) || (i2c == NULL) || (oled->initialized == 0U) ||
        (i2c->Instance != oled->config.i2c->Instance)) {
        return;
    }
    oled->busy = 0U;
    if (oled->remaining_pages > 0U) {
        oled->remaining_pages--;
    }
    oled->page++;
    if (oled->remaining_pages == 0U) {
        oled->page = 0U;
    } else {
        oled->refresh_requested = 1U;
    }
}

void driver_oled_error_isr(driver_oled_t *oled, I2C_HandleTypeDef *i2c)
{
    if ((oled != NULL) && (i2c != NULL) && (oled->initialized != 0U) &&
        (i2c->Instance == oled->config.i2c->Instance)) {
        oled->busy = 0U;
        oled->remaining_pages = 0U;
        oled->refresh_requested = 0U;
    }
}
