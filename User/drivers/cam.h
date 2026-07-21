#ifndef CAM_H
#define CAM_H

#include <stdint.h>
#include <stm32f4xx_hal.h>

/* 缓冲区、帧和发送队列容量。 */
typedef enum {
    CAM_DMA_RX_BUF_SIZE = 128,            /* DMA 单次接收缓冲区大小。 */
    CAM_RX_FIFO_SIZE = 512,               /* 接收环形队列容量。 */
    CAM_MAX_FRAME_LEN = 128,              /* 接收 payload 最大长度。 */
    CAM_TX_RAW_MAX_LEN = 128,             /* 单次原始发送最大长度。 */
    CAM_TX_PACKET_MAX_PAYLOAD_LEN = 128,  /* 单个协议包最大 payload 长度。 */
    CAM_TX_FRAME_MAX_LEN = 258,           /* 最坏转义后的协议帧最大长度。 */
    CAM_TX_QUEUE_CAPACITY = 4,            /* 待发送帧队列容量。 */
} cam_buf_size_t;

/* 帧定界与转义字节。 */
typedef enum {
    CAM_FRAME_TAIL = 0xFE,   /* 帧尾。 */
    CAM_FRAME_HEADER = 0xFF, /* 帧头。 */
    CAM_ESC_BYTE = 0x7D,     /* 转义前缀。 */
    CAM_ESC_TAIL = 0x5E,     /* ESC+5E 表示 0xFE。 */
    CAM_ESC_HEADER = 0x5F,   /* ESC+5F 表示 0xFF。 */
    CAM_ESC_ESC = 0x5D,      /* ESC+5D 表示 0x7D。 */
} cam_frame_byte_t;

/* 帧解析状态。 */
typedef enum {
    CAM_STATE_WAIT_HEADER = 0, /* 等待帧头。 */
    CAM_STATE_RECEIVING_DATA,  /* 接收 payload。 */
    CAM_STATE_ESCAPE,          /* 处理转义字节。 */
} cam_frame_state_t;

/* 发送接口状态。 */
typedef enum {
    CAM_STATUS_OK = 0,                /* 操作成功。 */
    CAM_STATUS_INVALID_ARGUMENT = -1, /* 参数无效。 */
    CAM_STATUS_NOT_INITIALIZED = -2,  /* 模块尚未初始化。 */
    CAM_STATUS_QUEUE_FULL = -3,       /* 发送队列已满。 */
    CAM_STATUS_BUSY = -4,             /* HAL 串口暂时忙。 */
    CAM_STATUS_IO_ERROR = -5,         /* 串口发送或接收错误。 */
} cam_status_t;

/* 摄像头最近一次有效解析数据。 */
typedef struct {
    int16_t error_x;    /* 目标中心相对画面中心的 X 偏差，单位为像素。 */
    int16_t error_y;    /* 目标中心相对画面中心的 Y 偏差，单位为像素。 */
    uint8_t has_target; /* 非零表示当前检测到有效目标。 */
    uint8_t switch_ack; /* 非零表示视觉端本帧已完成目标切换。 */
} cam_data_t;

/* DMA 发送帧存储，生命周期由 cam 模块管理。 */
typedef struct {
    uint8_t data[CAM_TX_FRAME_MAX_LEN]; /* 完整待发送字节流。 */
    uint16_t length;                    /* 实际发送长度。 */
} cam_tx_frame_t;

/* 摄像头单实例内部状态。 */
typedef struct {
    UART_HandleTypeDef *huart; /* 绑定的串口。 */

    uint8_t dma_rx_buffer[CAM_DMA_RX_BUF_SIZE]; /* DMA 接收缓冲区。 */
    uint8_t rx_fifo[CAM_RX_FIFO_SIZE];          /* 接收环形队列。 */
    volatile uint16_t rx_write_pos;             /* ISR 写入位置。 */
    volatile uint16_t rx_read_pos;              /* task 读取位置。 */

    cam_frame_state_t rx_state;              /* 当前接收解析状态。 */
    uint8_t frame_buffer[CAM_MAX_FRAME_LEN]; /* 接收帧 payload。 */
    uint16_t frame_index;                    /* payload 写入位置。 */

    cam_tx_frame_t tx_queue[CAM_TX_QUEUE_CAPACITY]; /* DMA 发送帧队列。 */
    volatile uint8_t tx_write_pos;                 /* 下一帧写入位置。 */
    volatile uint8_t tx_read_pos;                  /* 当前发送帧位置。 */
    volatile uint8_t tx_count;                     /* 队列中帧数量。 */
    volatile uint8_t is_tx_busy;                   /* DMA TX 忙标志。 */
    volatile cam_status_t last_status;             /* 最近一次发送状态。 */
} cam_handle_t;

/* 收到完整摄像头数据帧后置 1，由上层按需清零。 */
extern volatile uint8_t cam_frame_ready;

/* 生命周期、接收任务和数据读取接口。 */
void cam_init(UART_HandleTypeDef *huart);
void cam_task(void);
cam_data_t cam_get_data(void);
uint8_t cam_take_switch_ack(void);

/* 非阻塞发送接口，不允许在 ISR 中调用。 */
cam_status_t cam_send_packet(const uint8_t *payload, uint16_t length);
cam_status_t cam_send_raw(const uint8_t *data, uint16_t length);
cam_status_t cam_get_last_status(void);
uint8_t cam_get_pending_tx_count(void);

/* HAL UART 回调转发入口。 */
void cam_rx_callback(UART_HandleTypeDef *huart, uint16_t size);
void cam_tx_callback(UART_HandleTypeDef *huart);
void cam_error_callback(UART_HandleTypeDef *huart);

#endif /* CAM_H */
