#ifndef BLUETEETH_H
#define BLUETEETH_H

#include <stdint.h>
#include <stdarg.h>
#include <stm32f4xx_hal.h>

/* 缓冲区与帧参数 */
typedef enum {
    BLUETEETH_DMA_RX_BUF_SIZE = 128, /* DMA 单次接收最大缓存量 */
    BLUETEETH_DMA_TX_BUF_SIZE = 128, /* DMA 单次发送最大缓存量 */
    BLUETEETH_RX_FIFO_SIZE = 512,    /* 接收环形队列容量 */
    BLUETEETH_TX_FIFO_SIZE = 512,    /* 发送环形队列容量 */
    BLUETEETH_MAX_FRAME_LEN = 128,   /* 单帧协议最大长度 */
} blueteeth_buf_size_t;

/* 帧定界字节 */
typedef enum {
    BLUETEETH_FRAME_TAIL = '#',
    BLUETEETH_FRAME_HEADER = '@',
} blueteeth_frame_byte_t;

/* 帧解析状态 */
typedef enum {
    BLUETEETH_STATE_WAIT_HEADER = 0,
    BLUETEETH_STATE_RECEIVING_DATA,
} blueteeth_frame_state_t;

/* 蓝牙指令映射表条目 */
typedef struct {
    const char *cmd_string;       /* 指令字符串 */
    void (*cmd_handler)(void);    /* 回调函数 */
} blueteeth_command_map_t;

/* 蓝牙通信句柄 */
typedef struct {
    UART_HandleTypeDef *huart; /* 绑定的串口 */

    uint8_t dma_rx_buffer[BLUETEETH_DMA_RX_BUF_SIZE]; /* DMA 接收缓冲 */
    uint8_t rx_fifo[BLUETEETH_RX_FIFO_SIZE];          /* 接收环形队列 */
    volatile uint16_t rx_write_pos;                   /* FIFO 写指针（ISR 写入） */
    volatile uint16_t rx_read_pos;                    /* FIFO 读指针 */

    uint8_t dma_tx_buffer[BLUETEETH_DMA_TX_BUF_SIZE]; /* DMA 发送缓冲 */
    uint8_t tx_fifo[BLUETEETH_TX_FIFO_SIZE];          /* 发送环形队列 */
    volatile uint16_t tx_write_pos;                   /* FIFO 写指针 */
    volatile uint16_t tx_read_pos;                    /* FIFO 读指针 */
    volatile uint8_t is_tx_busy;                      /* 发送忙标志 */

    blueteeth_frame_state_t rx_state;           /* 当前解析状态 */
    uint8_t frame_buffer[BLUETEETH_MAX_FRAME_LEN]; /* 帧组装缓冲 */
    uint16_t frame_index;                       /* 帧缓冲写入位置 */
} blueteeth_handle_t;

void blueteeth_init(UART_HandleTypeDef *huart);
void blueteeth_printf(const char *format, ...);
void blueteeth_display(int16_t x, int16_t y, const char *format, ...);
void blueteeth_clear(void);
void blueteeth_plot(const char *format, ...);
void blueteeth_tx_callback(UART_HandleTypeDef *huart);
void blueteeth_rx_callback(UART_HandleTypeDef *huart, uint16_t size);
void blueteeth_task(void);

#endif
