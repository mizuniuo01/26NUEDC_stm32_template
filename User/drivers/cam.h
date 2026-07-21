#ifndef CAM_H
#define CAM_H

#include <stdint.h>
#include <stm32f4xx_hal.h>

/* 缓冲区、逻辑帧和发送队列容量。 */
typedef enum {
    CAM_DMA_RX_BUF_SIZE = 128,
    CAM_RX_FIFO_SIZE = 512,
    CAM_MAX_PAYLOAD_SIZE = 128,
    CAM_LOGICAL_FRAME_MAX_SIZE = 132,
    CAM_TX_FRAME_MAX_SIZE = 266,
    CAM_TX_QUEUE_CAPACITY = 4,
} cam_buffer_size_t;

/* 帧定界与转义字节。 */
typedef enum {
    CAM_FRAME_TAIL = 0xFE,
    CAM_FRAME_HEADER = 0xFF,
    CAM_ESCAPE_BYTE = 0x7D,
    CAM_ESCAPE_TAIL = 0x5E,
    CAM_ESCAPE_HEADER = 0x5F,
    CAM_ESCAPE_ESCAPE = 0x5D,
} cam_frame_byte_t;

/* 二进制协议版本和消息类型。 */
typedef enum {
    CAM_PROTOCOL_VERSION = 0x01,
    CAM_MESSAGE_TARGET_REPORT = 0x01,
    CAM_MESSAGE_SWITCH_COMMAND = 0x10,
} cam_message_type_t;

/* 目标报告 flags 字段。 */
typedef enum {
    CAM_TARGET_FLAG_HAS_TARGET = 0x01,
    CAM_TARGET_FLAG_SWITCH_ACK = 0x02,
} cam_target_flag_t;

/* 帧解析状态。 */
typedef enum {
    CAM_PARSE_WAIT_HEADER = 0,
    CAM_PARSE_RECEIVING,
    CAM_PARSE_ESCAPE,
} cam_parse_state_t;

/* 摄像头驱动状态。 */
typedef enum {
    CAM_STATUS_OK = 0,
    CAM_STATUS_INVALID_ARGUMENT = -1,
    CAM_STATUS_NOT_INITIALIZED = -2,
    CAM_STATUS_QUEUE_FULL = -3,
    CAM_STATUS_BUSY = -4,
    CAM_STATUS_IO_ERROR = -5,
} cam_status_t;

/* 摄像头最近一次有效目标报告。 */
typedef struct {
    int16_t error_x;
    int16_t error_y;
    uint8_t has_target;
    uint8_t switch_ack;
    uint8_t switch_ack_id;
} cam_data_t;

/* DMA 发送帧存储。 */
typedef struct {
    uint8_t data[CAM_TX_FRAME_MAX_SIZE];
    uint16_t length;
} cam_tx_frame_t;

/* 摄像头单实例内部状态。 */
typedef struct {
    UART_HandleTypeDef *huart;
    uint8_t dma_rx_buffer[CAM_DMA_RX_BUF_SIZE];
    uint8_t rx_fifo[CAM_RX_FIFO_SIZE];
    volatile uint16_t rx_write_pos;
    volatile uint16_t rx_read_pos;
    cam_parse_state_t parse_state;
    uint8_t logical_frame[CAM_LOGICAL_FRAME_MAX_SIZE];
    uint16_t logical_frame_length;
    cam_tx_frame_t tx_queue[CAM_TX_QUEUE_CAPACITY];
    volatile uint8_t tx_write_pos;
    volatile uint8_t tx_read_pos;
    volatile uint8_t tx_count;
    volatile uint8_t is_tx_busy;
    volatile cam_status_t last_status;
} cam_handle_t;

/* 生命周期和目标报告读取接口。 */
void cam_init(UART_HandleTypeDef *huart);
void cam_task(void);
cam_data_t cam_get_data(void);
uint8_t cam_take_data(cam_data_t *data);
uint8_t cam_take_switch_ack(uint8_t *request_id);

/* 非阻塞目标切换命令。 */
cam_status_t cam_send_switch_command(uint8_t switch_state, uint8_t request_id);
cam_status_t cam_get_last_status(void);
uint8_t cam_get_pending_tx_count(void);

/* HAL UART 回调转发入口。 */
void cam_rx_callback(UART_HandleTypeDef *huart, uint16_t size);
void cam_tx_callback(UART_HandleTypeDef *huart);
void cam_error_callback(UART_HandleTypeDef *huart);

#endif /* CAM_H */
