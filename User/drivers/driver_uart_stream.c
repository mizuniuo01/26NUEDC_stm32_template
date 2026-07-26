/**
 * @file driver_uart_stream.c
 * @brief DMA receive-to-idle UART stream driver implementation.
 */
#include "driver_uart_stream.h"

#include <string.h>

status_code_t driver_uart_stream_init(driver_uart_stream_t *stream, UART_HandleTypeDef *uart)
{
    HAL_StatusTypeDef result;

    if ((stream == NULL) || (uart == NULL)) {
        return STATUS_INVALID_ARGUMENT;
    }
    stream->uart = uart;
    stream->latest_length = 0U;
    stream->sequence = 0U;
    stream->tx_busy = 0U;
    stream->initialized = 1U;
    result = HAL_UARTEx_ReceiveToIdle_DMA(uart, stream->dma_buffer,
                                          sizeof(stream->dma_buffer));
    if ((result == HAL_OK) && (uart->hdmarx != NULL)) {
        __HAL_DMA_DISABLE_IT(uart->hdmarx, DMA_IT_HT);
    }
    if (result != HAL_OK) {
        stream->initialized = 0U;
    }
    return result == HAL_OK ? STATUS_OK : STATUS_IO_ERROR;
}

void driver_uart_stream_rx_event_isr(driver_uart_stream_t *stream, uint16_t size)
{
    uint16_t length;

    if ((stream == NULL) || (stream->initialized == 0U)) {
        return;
    }
    length = size > sizeof(stream->latest) ? sizeof(stream->latest) : size;
    (void)memcpy(stream->latest, stream->dma_buffer, length);
    stream->latest_length = length;
    stream->sequence++;
    (void)HAL_UARTEx_ReceiveToIdle_DMA(stream->uart, stream->dma_buffer,
                                       sizeof(stream->dma_buffer));
    if (stream->uart->hdmarx != NULL) {
        __HAL_DMA_DISABLE_IT(stream->uart->hdmarx, DMA_IT_HT);
    }
}

status_code_t driver_uart_stream_take(driver_uart_stream_t *stream, uint8_t *data,
                                      uint16_t capacity, uint16_t *length,
                                      uint32_t *sequence)
{
    uint16_t available;
    uint32_t interrupt_mask;

    if ((stream == NULL) || (data == NULL) || (length == NULL) ||
        (stream->initialized == 0U)) {
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
    if (sequence != NULL) {
        *sequence = stream->sequence;
    }
    return STATUS_OK;
}

status_code_t driver_uart_stream_write(driver_uart_stream_t *stream,
                                       const uint8_t *data, uint16_t length)
{
    if ((stream == NULL) || (data == NULL) || (stream->initialized == 0U)) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (length > sizeof(stream->tx_buffer)) {
        return STATUS_OUT_OF_RANGE;
    }
    if (stream->tx_busy != 0U) {
        return STATUS_BUSY;
    }
    (void)memcpy(stream->tx_buffer, data, length);
    stream->tx_busy = 1U;
    if (HAL_UART_Transmit_IT(stream->uart, stream->tx_buffer, length) != HAL_OK) {
        stream->tx_busy = 0U;
        return STATUS_IO_ERROR;
    }
    return STATUS_OK;
}

void driver_uart_stream_tx_complete_isr(driver_uart_stream_t *stream,
                                        UART_HandleTypeDef *uart)
{
    if ((stream != NULL) && (uart != NULL) && (stream->initialized != 0U) &&
        (uart->Instance == stream->uart->Instance)) {
        stream->tx_busy = 0U;
    }
}

void driver_uart_stream_error_isr(driver_uart_stream_t *stream,
                                  UART_HandleTypeDef *uart)
{
    if ((stream != NULL) && (uart != NULL) && (stream->initialized != 0U) &&
        (uart->Instance == stream->uart->Instance)) {
        stream->tx_busy = 0U;
        (void)HAL_UARTEx_ReceiveToIdle_DMA(stream->uart, stream->dma_buffer,
                                           sizeof(stream->dma_buffer));
        if (stream->uart->hdmarx != NULL) {
            __HAL_DMA_DISABLE_IT(stream->uart->hdmarx, DMA_IT_HT);
        }
    }
}
