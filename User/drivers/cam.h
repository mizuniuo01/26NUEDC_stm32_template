#ifndef CAM_H
#define CAM_H

#include <stdint.h>
#include <stm32f4xx_hal.h>

/* 缓冲区与帧参数 */
typedef enum {
    CAM_DMA_RX_BUF_SIZE = 128, /* DMA 单次接收最大缓存量 */
    CAM_RX_FIFO_SIZE = 512,    /* 接收环形队列容量 */
    CAM_MAX_FRAME_LEN = 128,   /* 单帧最大长度 */
} cam_buf_size_t;

/* 帧定界与转义字节 */
typedef enum {
    CAM_FRAME_TAIL   = 0xFE,
    CAM_FRAME_HEADER = 0xFF,
    CAM_ESC_BYTE     = 0x7D, /* 转义前缀 */
    CAM_ESC_TAIL     = 0x5E, /* ESC+5E → 0xFE */
    CAM_ESC_HEADER   = 0x5F, /* ESC+5F → 0xFF */
    CAM_ESC_ESC      = 0x5D, /* ESC+5D → 0x7D */
} cam_frame_byte_t;

#define CAM_FRAME_DATA_LEN 5 /* 帧数据字段数 */

/* 帧解析状态 */
typedef enum {
    CAM_STATE_WAIT_HEADER = 0,
    CAM_STATE_RECEIVING_DATA,
    CAM_STATE_ESCAPE,
} cam_frame_state_t;

/* 摄像头句柄 */
typedef struct {
    UART_HandleTypeDef *huart; /* 绑定的串口 */

    uint8_t dma_rx_buffer[CAM_DMA_RX_BUF_SIZE]; /* DMA 接收缓冲 */
    uint8_t rx_fifo[CAM_RX_FIFO_SIZE];          /* 接收环形队列 */
    uint16_t rx_write_pos;                      /* FIFO 写指针 */
    uint16_t rx_read_pos;                       /* FIFO 读指针 */

    cam_frame_state_t rx_state;           /* 当前解析状态 */
    uint8_t frame_buffer[CAM_MAX_FRAME_LEN]; /* 帧组装缓冲 */
    uint16_t frame_index;                 /* 帧缓冲写入位置 */
} cam_handle_t;

/* 摄像头解析数据 */
typedef struct {
    uint8_t is_junction; /* 是否在路口：1=路口，0=正常 */
    uint8_t direction;   /* 方向指令：0=直走，1=右转，2=左转 */
    uint8_t green;       /* 绿灯标志：1=检测到绿灯，0=无 */
    uint8_t stop;        /* 停车标志：1=STOP，0=无 */
    int8_t  deviation;   /* 巡线偏差：正值=偏右，负值=偏左（像素） */
} cam_data_t;

/* 帧就绪标志位（收到完整帧时置 1，外部读取后手动清零） */
extern volatile uint8_t cam_frame_ready;

void cam_init(UART_HandleTypeDef *huart);
void cam_rx_callback(UART_HandleTypeDef *huart, uint16_t size);
void cam_task(void);
cam_data_t cam_get_data(void);

#endif
