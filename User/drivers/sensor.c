/**
 * @file    sensor.c
 * @brief   感为科技八路灰度传感器驱动（I2C DMA 模式）
 * @author  mizuniuo01
 * @date    2026-05-25
 * @version 1.0.0
 * @note    仅适配带 MCU 的感为科技八路灰度传感器
 * @note    I2C 协议，7 位地址 0x4C，通过 DMA 发送 0xDD 命令读取数字量
 * @note    数据位序：低电平(0)=黑线，高电平(1)=白底，模块内部取反
 * @note    sensor_task 由 sensor_tick_flag 触发
 * @note    参数非法时通过 error_report(ERROR_SOURCE_SENSOR, DRV_ERR_PARAM) 上报
 *
 * @usage
 * ─────────────────────────────────────────────────────────
 * I2C DMA 模式：HAL_I2C_Mem_Read_DMA → 回调中位取反处理。
 *
 * ── 初始化 ──
 *
 * sensor_init(&hi2c1);
 *
 * ── ISR 中置 sensor_tick_flag ──
 *
 * ── 主循环 ──
 *
 * sensor_task();  // 检查 tick_flag → 发起 DMA 请求
 *
 * ── HAL 回调 ──
 *
 * void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
 * { sensor_rx_callback(hi2c); }
 * void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
 * { sensor_error_callback(hi2c); }
 *
 * ── 数据读取 ──
 *
 * uint8_t data = sensor_read_data();
 * // bit0=最左侧探头 ... bit7=最右侧探头，1=黑线
 */

#include "sensor.h"
#include "error_handler.h"

static I2C_HandleTypeDef *sensor_hi2c;
static uint8_t sensor_rx_buffer[1];
static volatile uint8_t sensor_processed_data;
static volatile uint8_t dma_busy;

volatile uint8_t sensor_tick_flag;

/**
 * @brief  传感器 I2C 硬件初始化
 * @param  hi2c  I2C 外设句柄
 * @retval 无
 */
void sensor_init(I2C_HandleTypeDef *hi2c)
{
    if (!hi2c) {
        error_report(ERROR_SOURCE_SENSOR, DRV_ERR_PARAM);
        return;
    }

    sensor_hi2c = hi2c;
    dma_busy = 0;
}

/**
 * @brief  获取 DMA 忙闲状态
 * @param  无
 * @retval 当前状态
 */
sensor_state_t sensor_get_state(void)
{
    return dma_busy ? SENSOR_STATE_BUSY : SENSOR_STATE_IDLE;
}

/**
 * @brief  发起 DMA 读取传感器数据
 * @note   由 sensor_task 调用，非阻塞
 * @param  无
 * @retval 无
 */
void sensor_request_dma(void)
{
    HAL_StatusTypeDef status;

    if (!sensor_hi2c) {
        error_report(ERROR_SOURCE_SENSOR, DRV_ERR_PARAM);
        return;
    }

    if (dma_busy) {
        error_report(ERROR_SOURCE_SENSOR, DRV_ERR_BUSY);
        return;
    }

    status = HAL_I2C_Mem_Read_DMA(sensor_hi2c, SENSOR_I2C_ADDR, SENSOR_CMD_READ_DIG,
        I2C_MEMADD_SIZE_8BIT, sensor_rx_buffer, 1);

    if (status == HAL_OK) {
        dma_busy = 1;
    } else {
        dma_busy = 0;
        if (status == HAL_BUSY) {
            error_report(ERROR_SOURCE_SENSOR, DRV_ERR_BUSY);
        } else if (status == HAL_TIMEOUT) {
            error_report(ERROR_SOURCE_SENSOR, DRV_ERR_TIMEOUT);
        } else {
            error_report(ERROR_SOURCE_SENSOR, DRV_ERR_IO);
        }
    }
}

/**
 * @brief  I2C DMA 接收完成回调（ISR 中调用）
 * @note   对原始数据做位取反：低电平(0)=黑线 → 正逻辑(1=黑线)
 * @param  hi2c  I2C 句柄
 * @retval 无
 */
void sensor_rx_callback(I2C_HandleTypeDef *hi2c)
{
    uint8_t raw_data;
    uint8_t temp_data;
    uint8_t i;

    if (!sensor_hi2c) {
        error_report(ERROR_SOURCE_SENSOR, DRV_ERR_PARAM);
        return;
    }

    if (hi2c->Instance != sensor_hi2c->Instance) {
        error_report(ERROR_SOURCE_SENSOR, DRV_ERR_PARAM);
        return;
    }

    dma_busy = 0;

    raw_data = sensor_rx_buffer[0];
    temp_data = 0;

    /*
     * 位序标准：bit0=最左探头, bit7=最右探头
     * 原始数据低电平(0)=黑线，取反转为正逻辑(1=黑线)
     */
    for (i = 0; i < 8; i++) {
        if (((raw_data >> i) & 0x01) == 0) {
            temp_data |= (1 << i);
        }
    }

    sensor_processed_data = temp_data;
}

/**
 * @brief  I2C 错误回调（ISR 中调用）
 * @param  hi2c  I2C 句柄
 * @retval 无
 */
void sensor_error_callback(I2C_HandleTypeDef *hi2c)
{
    if (!hi2c || !sensor_hi2c) {
        error_report(ERROR_SOURCE_SENSOR, DRV_ERR_PARAM);
        return;
    }

    if (hi2c->Instance != sensor_hi2c->Instance) {
        error_report(ERROR_SOURCE_SENSOR, DRV_ERR_PARAM);
        return;
    }

    dma_busy = 0;
    error_report(ERROR_SOURCE_SENSOR, DRV_ERR_IO);
}

/**
 * @brief  读取转换后的传感器数据
 * @param  无
 * @retval 8 位灰度数据（bit0=最左, 1=黑线）
 */
uint8_t sensor_read_data(void)
{
    return sensor_processed_data;
}

/**
 * @brief  传感器轮询任务（主循环中调用）
 * @note   由 sensor_tick_flag 触发
 * @param  无
 * @retval 无
 */
void sensor_task(void)
{
    if (sensor_tick_flag) {
        sensor_tick_flag = 0;
        sensor_request_dma();
    }
}
