/**
 * @file driver_oled.c
 * @brief 通过 I2C 管理 SSD1306 显存并执行非阻塞分页刷新。
 */
#include "driver_oled.h"
#include <stddef.h>
#include <string.h>

/* clang-format off: 初始化命令按 SSD1306 数据手册顺序紧凑排列。 */
static const uint8_t init_sequence[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14,
    0x20, 0x02, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0x7F, 0xD9, 0xF1,
    0xDB, 0x30, 0xA4, 0xA6, 0xAF,
};
/* clang-format on */

/**
 * @brief  启动一次 I2C3 中断发送
 * @param  oled OLED 驱动实例
 * @param  length 待发送字节数
 * @param  transfer 本次发送阶段
 * @retval STATUS_OK I2C3 中断发送已启动
 * @retval STATUS_BUSY I2C3 正忙
 * @retval STATUS_IO_ERROR HAL 无法启动发送
 */
static status_code_t start_transfer(driver_oled_t *oled, uint16_t length,
    driver_oled_transfer_t transfer)
{
    HAL_StatusTypeDef result;

    oled->is_busy = true;
    oled->transfer = transfer;
    result = HAL_I2C_Master_Transmit_IT(oled->config.i2c, oled->config.address, oled->tx_buffer,
        length);
    if (result != HAL_OK) {
        oled->is_busy = false;
        oled->transfer = DRIVER_OLED_TRANSFER_IDLE;
        return result == HAL_BUSY ? STATUS_BUSY : STATUS_IO_ERROR;
    }
    return STATUS_OK;
}

/**
 * @brief  初始化 SSD1306 控制器和驱动状态
 * @param  oled OLED 驱动实例
 * @param  config I2C 句柄和设备地址配置
 * @retval STATUS_OK SSD1306 异步初始化状态已建立
 * @retval STATUS_INVALID_ARGUMENT 实例、配置或 I2C 句柄为空
 */
status_code_t driver_oled_init(driver_oled_t *oled, const driver_oled_config_t *config)
{
    if (!oled || !config || !config->i2c) {
        return STATUS_INVALID_ARGUMENT;
    }
    oled->config = *config;
    oled->transfer = DRIVER_OLED_TRANSFER_IDLE;
    oled->is_busy = false;
    oled->is_refresh_requested = false;
    oled->is_ready = false;
    oled->has_fault = false;
    oled->init_index = 0U;
    oled->page = 0U;
    oled->is_initialized = true;
    driver_oled_clear(oled);
    return STATUS_OK;
}

/**
 * @brief  将 OLED 单色显存全部清零
 * @param  oled OLED 驱动实例，为空时不执行操作
 */
void driver_oled_clear(driver_oled_t *oled)
{
    if (oled) {
        (void)memset(oled->buffer, 0, sizeof(oled->buffer));
    }
}

/**
 * @brief  设置 OLED 显存中的一个像素
 * @param  oled OLED 驱动实例
 * @param  x 像素横坐标
 * @param  y 像素纵坐标
 * @param  is_on 像素点亮标志
 * @retval STATUS_OK 像素已更新
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 * @retval STATUS_OUT_OF_RANGE 像素坐标超出屏幕范围
 */
status_code_t driver_oled_set_pixel(driver_oled_t *oled, uint8_t x, uint8_t y, bool is_on)
{
    size_t index;
    uint8_t mask;

    if (!oled || !oled->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if ((x >= DRIVER_OLED_WIDTH_PIXELS) || (y >= DRIVER_OLED_HEIGHT_PIXELS)) {
        return STATUS_OUT_OF_RANGE;
    }
    index = (size_t)x + (size_t)(y / 8U) * DRIVER_OLED_WIDTH_PIXELS;
    mask = (uint8_t)(1U << (y % 8U));
    if (is_on) {
        oled->buffer[index] |= mask;
    } else {
        oled->buffer[index] &= (uint8_t)~mask;
    }
    return STATUS_OK;
}

/**
 * @brief  请求一次完整显存的非阻塞分页刷新
 * @param  oled OLED 驱动实例
 * @retval STATUS_OK 刷新请求已登记
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 * @retval STATUS_BUSY 上一次刷新尚未完成
 */
status_code_t driver_oled_refresh(driver_oled_t *oled)
{
    if (!oled || !oled->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (oled->is_refresh_requested || (oled->page != 0U)) {
        return STATUS_BUSY;
    }
    oled->page = 0U;
    oled->is_refresh_requested = true;
    return STATUS_OK;
}

/**
 * @brief  推进一次 OLED 非阻塞分页刷新
 * @param  oled OLED 驱动实例
 * @retval STATUS_OK 当前无需发送或一次 I2C3 中断发送已启动
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 * @retval STATUS_BUSY I2C 外设正在执行其他事务
 * @retval STATUS_IO_ERROR HAL 无法启动 I2C 中断发送
 */
status_code_t driver_oled_process(driver_oled_t *oled)
{
    size_t offset;

    if (!oled || !oled->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (oled->has_fault) {
        return STATUS_IO_ERROR;
    }
    if (oled->is_busy) {
        return STATUS_OK;
    }
    if (!oled->is_ready) {
        oled->tx_buffer[0] = 0x00U;
        oled->tx_buffer[1] = init_sequence[oled->init_index];
        return start_transfer(oled, 2U, DRIVER_OLED_TRANSFER_INIT);
    }
    if (!oled->is_refresh_requested) {
        return STATUS_OK;
    }
    if (oled->transfer != DRIVER_OLED_TRANSFER_PAGE_DATA) {
        oled->tx_buffer[0] = 0x00U;
        oled->tx_buffer[1] = (uint8_t)(0xB0U | oled->page);
        oled->tx_buffer[2] = 0x00U;
        oled->tx_buffer[3] = 0x10U;
        return start_transfer(oled, 4U, DRIVER_OLED_TRANSFER_PAGE_COMMAND);
    }
    offset = (size_t)oled->page * DRIVER_OLED_WIDTH_PIXELS;
    oled->tx_buffer[0] = 0x40U;
    (void)memcpy(&oled->tx_buffer[1], &oled->buffer[offset], DRIVER_OLED_WIDTH_PIXELS);
    return start_transfer(oled, sizeof(oled->tx_buffer), DRIVER_OLED_TRANSFER_PAGE_DATA);
}

/**
 * @brief  处理 OLED 一页 I2C 发送完成事件
 * @param  oled OLED 驱动实例
 * @param  i2c 发生完成事件的 STM32 HAL I2C 句柄
 */
void driver_oled_tx_complete_isr(driver_oled_t *oled, I2C_HandleTypeDef *i2c)
{
    if (!oled || !i2c || !oled->is_initialized ||
        (i2c->Instance != oled->config.i2c->Instance)) {
        return;
    }
    oled->is_busy = false;
    if (oled->transfer == DRIVER_OLED_TRANSFER_INIT) {
        oled->init_index++;
        if (oled->init_index >= sizeof(init_sequence)) {
            oled->is_ready = true;
            oled->transfer = DRIVER_OLED_TRANSFER_IDLE;
        }
    } else if (oled->transfer == DRIVER_OLED_TRANSFER_PAGE_COMMAND) {
        oled->transfer = DRIVER_OLED_TRANSFER_PAGE_DATA;
    } else if (oled->transfer == DRIVER_OLED_TRANSFER_PAGE_DATA) {
        oled->page++;
        oled->transfer = DRIVER_OLED_TRANSFER_IDLE;
        if (oled->page >= 8U) {
            oled->page = 0U;
            oled->is_refresh_requested = false;
        }
    }
}

/**
 * @brief  处理 OLED I2C 错误并终止当前刷新
 * @param  oled OLED 驱动实例
 * @param  i2c 发生错误事件的 STM32 HAL I2C 句柄
 */
void driver_oled_error_isr(driver_oled_t *oled, I2C_HandleTypeDef *i2c)
{
    if (oled && i2c && oled->is_initialized &&
        (i2c->Instance == oled->config.i2c->Instance)) {
        oled->is_busy = false;
        oled->transfer = DRIVER_OLED_TRANSFER_IDLE;
        oled->is_refresh_requested = false;
        oled->has_fault = true;
    }
}
