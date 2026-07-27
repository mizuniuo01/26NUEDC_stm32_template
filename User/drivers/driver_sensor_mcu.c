/**
 * @file driver_sensor_mcu.c
 * @brief 通过 I2C DMA 非阻塞采集 MCU 版七路巡线传感器。
 */
#include "driver_sensor_mcu.h"

static uint8_t receive_byte;

/**
 * @brief  初始化 MCU 版巡线传感器驱动
 * @param  sensor 巡线传感器驱动实例
 * @param  config I2C 句柄、设备地址和读取命令配置
 * @retval STATUS_OK 驱动已初始化
 * @retval STATUS_INVALID_ARGUMENT 实例、配置或 I2C 句柄为空
 */
status_code_t driver_sensor_mcu_init(driver_sensor_mcu_t *sensor,
    const driver_sensor_mcu_config_t *config)
{
    if (!sensor || !config || !config->i2c) {
        return STATUS_INVALID_ARGUMENT;
    }
    sensor->config = *config;
    sensor->raw = 0U;
    sensor->is_busy = false;
    sensor->is_valid = false;
    sensor->sequence = 0U;
    sensor->timestamp_ms = 0U;
    sensor->is_initialized = true;
    return STATUS_OK;
}

/**
 * @brief  请求一次非阻塞巡线传感器采集
 * @param  sensor 巡线传感器驱动实例
 * @retval STATUS_OK I2C DMA 读取已启动
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 * @retval STATUS_BUSY 上一次事务未结束或 I2C 外设正忙
 * @retval STATUS_TIMEOUT HAL 启动事务时报告超时
 * @retval STATUS_IO_ERROR HAL 无法启动 I2C DMA 读取
 */
status_code_t driver_sensor_mcu_request(driver_sensor_mcu_t *sensor)
{
    HAL_StatusTypeDef result;

    if (!sensor || !sensor->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (sensor->is_busy) {
        return STATUS_BUSY;
    }
    sensor->is_busy = true;
    result = HAL_I2C_Mem_Read_DMA(sensor->config.i2c, sensor->config.address,
        sensor->config.command, I2C_MEMADD_SIZE_8BIT, &receive_byte, 1U);
    if (result != HAL_OK) {
        sensor->is_busy = false;
        return result == HAL_BUSY ? STATUS_BUSY
                                  : (result == HAL_TIMEOUT ? STATUS_TIMEOUT : STATUS_IO_ERROR);
    }
    return STATUS_OK;
}

/**
 * @brief  处理巡线传感器 I2C DMA 读取完成事件
 * @param  sensor 巡线传感器驱动实例
 * @param  i2c 发生完成事件的 STM32 HAL I2C 句柄
 * @param  timestamp_ms 数据完成采集的时间，单位：毫秒
 */
void driver_sensor_mcu_rx_complete_isr(driver_sensor_mcu_t *sensor, I2C_HandleTypeDef *i2c,
    uint32_t timestamp_ms)
{
    if (!sensor || !i2c || !sensor->is_initialized ||
        (i2c->Instance != sensor->config.i2c->Instance)) {
        return;
    }
    sensor->raw = (uint8_t)~receive_byte;
    sensor->is_busy = false;
    sensor->is_valid = true;
    sensor->sequence++;
    sensor->timestamp_ms = timestamp_ms;
}

/**
 * @brief  处理巡线传感器 I2C 错误并使当前数据失效
 * @param  sensor 巡线传感器驱动实例
 * @param  i2c 发生错误事件的 STM32 HAL I2C 句柄
 */
void driver_sensor_mcu_error_isr(driver_sensor_mcu_t *sensor, I2C_HandleTypeDef *i2c)
{
    if (sensor && i2c && sensor->is_initialized &&
        (i2c->Instance == sensor->config.i2c->Instance)) {
        sensor->is_busy = false;
        sensor->is_valid = false;
    }
}

/**
 * @brief  获取最近一次巡线传感器采集快照
 * @param  sensor 巡线传感器驱动实例
 * @param  value 可选的巡线位掩码输出地址
 * @param  is_valid 可选的数据有效标志输出地址
 * @param  sequence 可选的数据更新序号输出地址
 * @param  timestamp_ms 可选的最近更新时间输出地址，单位：毫秒
 * @retval STATUS_OK 所有非空输出地址均已写入
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 */
status_code_t driver_sensor_mcu_snapshot(const driver_sensor_mcu_t *sensor, uint8_t *value,
    bool *is_valid, uint32_t *sequence, uint32_t *timestamp_ms)
{
    if (!sensor || !sensor->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (value) {
        *value = sensor->raw;
    }
    if (is_valid) {
        *is_valid = sensor->is_valid;
    }
    if (sequence) {
        *sequence = sensor->sequence;
    }
    if (timestamp_ms) {
        *timestamp_ms = sensor->timestamp_ms;
    }
    return STATUS_OK;
}
