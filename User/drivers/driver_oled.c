/**
 * @file driver_oled.c
 * @brief 通过 I2C 管理 SSD1306 显存并执行非阻塞分页刷新。
 */
#include "driver_oled.h"
#include <stddef.h>
#include <string.h>

/**
 * @brief  通过阻塞 I2C 事务发送一条 SSD1306 命令
 * @param  oled 已配置 I2C 的 OLED 驱动实例
 * @param  command SSD1306 命令字节
 * @retval STATUS_OK 命令发送成功
 * @retval STATUS_IO_ERROR HAL 报告 I2C 发送失败
 */
static status_code_t send_command(driver_oled_t *oled, uint8_t command)
{
    uint8_t packet[2] = {
        0x00U,
        command,
    };
    return HAL_I2C_Master_Transmit(oled->config.i2c, oled->config.address, packet, sizeof(packet),
               20U) == HAL_OK
               ? STATUS_OK
               : STATUS_IO_ERROR;
}

/**
 * @brief  初始化 SSD1306 控制器和驱动状态
 * @param  oled OLED 驱动实例
 * @param  config I2C 句柄和设备地址配置
 * @retval STATUS_OK SSD1306 初始化命令全部发送成功
 * @retval STATUS_INVALID_ARGUMENT 实例、配置或 I2C 句柄为空
 * @retval STATUS_IO_ERROR 任一初始化命令发送失败
 */
status_code_t driver_oled_init(driver_oled_t *oled, const driver_oled_config_t *config)
{
    /* clang-format off: 初始化命令按 SSD1306 数据手册顺序紧凑排列 */
    static const uint8_t init_sequence[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14,
        0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0x7F, 0xD9, 0xF1,
        0xDB, 0x40, 0xA4, 0xA6, 0xAF,
    };
    uint8_t i;
    /* clang-format on */
    if (!oled || !config || !config->i2c) {
        return STATUS_INVALID_ARGUMENT;
    }
    oled->config = *config;
    oled->is_busy = false;
    oled->is_refresh_requested = false;
    oled->page = 0U;
    oled->remaining_pages = 0U;
    oled->is_initialized = true;
    driver_oled_clear(oled);
    for (i = 0U; i < sizeof(init_sequence); i++) {
        if (send_command(oled, init_sequence[i]) != STATUS_OK) {
            oled->is_initialized = false;
            return STATUS_IO_ERROR;
        }
    }
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
    if (oled->is_busy || (oled->remaining_pages != 0U)) {
        return STATUS_BUSY;
    }
    oled->page = 0U;
    oled->remaining_pages = 8U;
    oled->is_refresh_requested = true;
    return STATUS_OK;
}

/**
 * @brief  推进一次 OLED 非阻塞分页刷新
 * @param  oled OLED 驱动实例
 * @retval STATUS_OK 当前无需发送或一页 DMA 发送已启动
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 * @retval STATUS_BUSY I2C 外设正在执行其他事务
 * @retval STATUS_IO_ERROR HAL 无法启动 I2C 中断发送
 */
status_code_t driver_oled_process(driver_oled_t *oled)
{
    size_t offset;
    HAL_StatusTypeDef result;

    if (!oled || !oled->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (oled->is_busy || !oled->is_refresh_requested) {
        return STATUS_OK;
    }
    offset = (size_t)oled->page * DRIVER_OLED_WIDTH_PIXELS;
    oled->tx_buffer[0] = 0x40U;
    (void)memcpy(&oled->tx_buffer[1], &oled->buffer[offset], DRIVER_OLED_WIDTH_PIXELS);
    oled->is_busy = true;
    result = HAL_I2C_Master_Transmit_IT(oled->config.i2c, oled->config.address, oled->tx_buffer,
        sizeof(oled->tx_buffer));
    if (result != HAL_OK) {
        oled->is_busy = false;
        return result == HAL_BUSY ? STATUS_BUSY : STATUS_IO_ERROR;
    }
    oled->is_refresh_requested = false;
    return STATUS_OK;
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
    if (oled->remaining_pages > 0U) {
        oled->remaining_pages--;
    }
    oled->page++;
    if (oled->remaining_pages == 0U) {
        oled->page = 0U;
    } else {
        oled->is_refresh_requested = true;
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
        oled->remaining_pages = 0U;
        oled->is_refresh_requested = false;
    }
}
