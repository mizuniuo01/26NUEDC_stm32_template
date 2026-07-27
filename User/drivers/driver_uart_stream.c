/**
 * @file driver_uart_stream.c
 * @brief 管理串口接收到空闲 DMA 和异步发送缓冲区。
 */
#include "driver_uart_stream.h"
#include <string.h>

/**
 * @brief  初始化串口数据流并启动接收到空闲 DMA
 * @param  stream 串口数据流实例
 * @param  uart STM32 HAL 串口句柄
 * @retval STATUS_OK 接收到空闲 DMA 已启动
 * @retval STATUS_INVALID_ARGUMENT stream 或 uart 为空
 * @retval STATUS_IO_ERROR HAL 无法启动接收到空闲 DMA
 */
status_code_t driver_uart_stream_init(driver_uart_stream_t *stream, UART_HandleTypeDef *uart)
{
    HAL_StatusTypeDef result;

    if (!stream || !uart) {
        return STATUS_INVALID_ARGUMENT;
    }
    stream->uart = uart;
    stream->latest_length = 0U;
    stream->sequence = 0U;
    stream->is_tx_busy = false;
    stream->is_initialized = true;
    result = HAL_UARTEx_ReceiveToIdle_DMA(uart, stream->dma_buffer, sizeof(stream->dma_buffer));
    if ((result == HAL_OK) && uart->hdmarx) {
        __HAL_DMA_DISABLE_IT(uart->hdmarx, DMA_IT_HT);
    }
    if (result != HAL_OK) {
        stream->is_initialized = false;
    }
    return result == HAL_OK ? STATUS_OK : STATUS_IO_ERROR;
}

/**
 * @brief  保存一次接收到空闲数据并重新启动 DMA 接收
 * @param  stream 串口数据流实例
 * @param  size 当前 DMA 缓冲区收到的字节数
 */
void driver_uart_stream_rx_event_isr(driver_uart_stream_t *stream, uint16_t size)
{
    uint16_t length;

    if (!stream || !stream->is_initialized) {
        return;
    }
    length = size > sizeof(stream->latest) ? sizeof(stream->latest) : size;
    (void)memcpy(stream->latest, stream->dma_buffer, length);
    stream->latest_length = length;
    stream->sequence++;
    (void)HAL_UARTEx_ReceiveToIdle_DMA(stream->uart, stream->dma_buffer,
        sizeof(stream->dma_buffer));
    if (stream->uart->hdmarx) {
        __HAL_DMA_DISABLE_IT(stream->uart->hdmarx, DMA_IT_HT);
    }
}

/**
 * @brief  原子地取走最近一次接收数据
 * @param  stream 串口数据流实例
 * @param  data 接收数据副本的缓冲区
 * @param  capacity data 的容量，单位：字节
 * @param  length 接收实际复制字节数
 * @param  sequence 可选的数据更新序号输出地址
 * @retval STATUS_OK 当前快照已经取走
 * @retval STATUS_INVALID_ARGUMENT 必需地址为空或数据流尚未初始化
 */
status_code_t driver_uart_stream_take(driver_uart_stream_t *stream, uint8_t *data,
    uint16_t capacity, uint16_t *length, uint32_t *sequence)
{
    uint16_t available;
    uint32_t interrupt_mask;

    if (!stream || !data || !length || !stream->is_initialized) {
        return STATUS_INVALID_ARGUMENT;
    }
    interrupt_mask = __get_PRIMASK();
    __disable_irq();
    available = stream->latest_length;
    if (available > capacity) {
        available = capacity;
    }
    (void)memcpy(data, stream->latest, available);
    stream->latest_length = 0U;
    __set_PRIMASK(interrupt_mask);
    *length = available;
    if (sequence) {
        *sequence = stream->sequence;
    }
    return STATUS_OK;
}

/**
 * @brief  复制并通过 DMA 异步发送一段串口字节流
 * @param  stream 串口数据流实例
 * @param  data 待发送字节序列
 * @param  length 待发送字节数
 * @retval STATUS_OK 串口中断发送已启动
 * @retval STATUS_INVALID_ARGUMENT 必需地址为空或数据流尚未初始化
 * @retval STATUS_OUT_OF_RANGE length 超过发送缓冲区容量
 * @retval STATUS_BUSY 上一次串口发送尚未完成
 * @retval STATUS_IO_ERROR 未配置发送 DMA 或 HAL 无法启动 DMA 发送
 */
status_code_t driver_uart_stream_write(driver_uart_stream_t *stream, const uint8_t *data,
    uint16_t length)
{
    if (!stream || !data || !stream->is_initialized) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (length > sizeof(stream->tx_buffer)) {
        return STATUS_OUT_OF_RANGE;
    }
    if (stream->is_tx_busy) {
        return STATUS_BUSY;
    }
    (void)memcpy(stream->tx_buffer, data, length);
    stream->is_tx_busy = true;
    if (!stream->uart->hdmatx ||
        (HAL_UART_Transmit_DMA(stream->uart, stream->tx_buffer, length) != HAL_OK)) {
        stream->is_tx_busy = false;
        return STATUS_IO_ERROR;
    }
    return STATUS_OK;
}

/**
 * @brief  处理串口发送完成事件
 * @param  stream 串口数据流实例
 * @param  uart 发生完成事件的 STM32 HAL 串口句柄
 */
void driver_uart_stream_tx_complete_isr(driver_uart_stream_t *stream, UART_HandleTypeDef *uart)
{
    if (stream && uart && stream->is_initialized &&
        (uart->Instance == stream->uart->Instance)) {
        stream->is_tx_busy = false;
    }
}

/**
 * @brief  处理串口错误、释放发送状态并尝试恢复 DMA 接收
 * @param  stream 串口数据流实例
 * @param  uart 发生错误事件的 STM32 HAL 串口句柄
 */
void driver_uart_stream_error_isr(driver_uart_stream_t *stream, UART_HandleTypeDef *uart)
{
    if (stream && uart && stream->is_initialized &&
        (uart->Instance == stream->uart->Instance)) {
        stream->is_tx_busy = false;
        (void)HAL_UARTEx_ReceiveToIdle_DMA(stream->uart, stream->dma_buffer,
            sizeof(stream->dma_buffer));
        if (stream->uart->hdmarx) {
            __HAL_DMA_DISABLE_IT(stream->uart->hdmarx, DMA_IT_HT);
        }
    }
}
