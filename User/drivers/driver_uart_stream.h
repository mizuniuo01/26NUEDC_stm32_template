/**
 * @file driver_uart_stream.h
 * @brief DMA receive-to-idle UART stream driver.
 */
#ifndef USER_DRIVERS_DRIVER_UART_STREAM_H
#define USER_DRIVERS_DRIVER_UART_STREAM_H

#include "main.h"
#include "status.h"

#define DRIVER_UART_STREAM_BUFFER_SIZE 128U

typedef struct {
    UART_HandleTypeDef *uart;
    uint8_t dma_buffer[DRIVER_UART_STREAM_BUFFER_SIZE];
    uint8_t latest[DRIVER_UART_STREAM_BUFFER_SIZE];
    uint8_t tx_buffer[DRIVER_UART_STREAM_BUFFER_SIZE];
    volatile uint16_t latest_length;
    volatile uint32_t sequence;
    volatile uint8_t tx_busy;
    uint8_t initialized;
} driver_uart_stream_t;

status_code_t driver_uart_stream_init(driver_uart_stream_t *stream, UART_HandleTypeDef *uart);
void driver_uart_stream_rx_event_isr(driver_uart_stream_t *stream, uint16_t size);
status_code_t driver_uart_stream_take(driver_uart_stream_t *stream, uint8_t *data,
                                      uint16_t capacity, uint16_t *length,
                                      uint32_t *sequence);
status_code_t driver_uart_stream_write(driver_uart_stream_t *stream,
                                       const uint8_t *data, uint16_t length);
void driver_uart_stream_tx_complete_isr(driver_uart_stream_t *stream,
                                        UART_HandleTypeDef *uart);
void driver_uart_stream_error_isr(driver_uart_stream_t *stream,
                                  UART_HandleTypeDef *uart);

#endif
