#ifndef AUTO_BALL_CAR_USER_DRIVERS_DRIVER_UART_STREAM_H
#define AUTO_BALL_CAR_USER_DRIVERS_DRIVER_UART_STREAM_H /* 头文件保护 */

#include "status.h"
#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

#define DRIVER_UART_STREAM_BUFFER_CAPACITY 128U /* 单次收发缓冲区容量，单位：字节 */

/* DMA 收发串口流驱动实例 */
/* 串口回调写接收快照，主循环在关中断临界区取走；发送忙标志按 bool 原子访问。 */
typedef struct {
    UART_HandleTypeDef *uart;                         /* CubeMX 生成的串口句柄 */
    uint8_t dma_buffer[DRIVER_UART_STREAM_BUFFER_CAPACITY]; /* DMA 接收缓冲区 */
    uint8_t latest[DRIVER_UART_STREAM_BUFFER_CAPACITY];     /* 最近一次接收数据副本 */
    uint8_t tx_buffer[DRIVER_UART_STREAM_BUFFER_CAPACITY];  /* DMA 发送缓冲区 */
    volatile uint16_t latest_length;                    /* 最近一次接收字节数 */
    volatile uint32_t sequence;                         /* 接收数据更新序号 */
    volatile bool is_tx_busy;                           /* DMA 发送进行中标志 */
    bool is_initialized;                                /* 驱动初始化完成标志 */
} driver_uart_stream_t;

/* 生命周期与接收数据接口 */
status_code_t driver_uart_stream_init(driver_uart_stream_t *stream, UART_HandleTypeDef *uart);
void driver_uart_stream_rx_event_isr(driver_uart_stream_t *stream, uint16_t size);
status_code_t driver_uart_stream_take(driver_uart_stream_t *stream, uint8_t *data,
    uint16_t capacity, uint16_t *length, uint32_t *sequence);

/* 发送与 HAL 中断完成回调转发接口 */
status_code_t driver_uart_stream_write(driver_uart_stream_t *stream, const uint8_t *data,
    uint16_t length);
void driver_uart_stream_tx_complete_isr(driver_uart_stream_t *stream, UART_HandleTypeDef *uart);
void driver_uart_stream_error_isr(driver_uart_stream_t *stream, UART_HandleTypeDef *uart);

#endif /* AUTO_BALL_CAR_USER_DRIVERS_DRIVER_UART_STREAM_H */
