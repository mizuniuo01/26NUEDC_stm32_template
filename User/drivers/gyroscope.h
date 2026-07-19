#ifndef GYROSCOPE_H
#define GYROSCOPE_H

#include <stdint.h>
#include <stm32f4xx_hal.h>

/* 缓冲区与帧参数 */
typedef enum {
    GYRO_DMA_RX_BUF_SIZE = 128, /* DMA 单次接收最大缓存量 */
    GYRO_RX_FIFO_SIZE = 512,    /* 接收环形队列容量 */
    GYRO_FRAME_LEN = 11,        /* 角度数据帧长度（含校验） */
} gyro_buf_size_t;

/* 单周期偏航角变化上限（度） */
#define GYRO_MAX_DELTA_DEG 50.0f

/* 帧类型字节 */
typedef enum {
    GYRO_TYPE_ANGLE = 0x53,
    GYRO_FRAME_HEADER = 0x55,
} gyro_frame_byte_t;

/* 帧解析状态 */
typedef enum {
    GYRO_STATE_WAIT_HEADER = 0,
    GYRO_STATE_WAIT_TYPE,
    GYRO_STATE_RECEIVING_DATA,
} gyro_frame_state_t;

/* 姿态角数据（度） */
typedef struct {
    float roll;  /* 横滚角 */
    float pitch; /* 俯仰角 */
    float yaw;   /* 偏航角 */
} gyro_data_t;

/* 姿态传感器句柄 */
typedef struct {
    UART_HandleTypeDef *huart; /* 绑定的串口 */

    uint8_t dma_rx_buffer[GYRO_DMA_RX_BUF_SIZE]; /* DMA 接收缓冲 */
    uint8_t rx_fifo[GYRO_RX_FIFO_SIZE];          /* 接收环形队列 */
    uint16_t rx_write_pos;                       /* FIFO 写指针 */
    uint16_t rx_read_pos;                        /* FIFO 读指针 */

    gyro_frame_state_t rx_state;           /* 当前解析状态 */
    uint8_t frame_buffer[GYRO_FRAME_LEN];  /* 帧组装缓冲 */
    uint16_t frame_index;                  /* 帧缓冲写入位置 */
} gyro_handle_t;

/* gyro_task 中 yaw 增量更新标志位（ISR 置1，task 清0） */
extern volatile uint8_t gyro_tick_flag;

void gyro_init(UART_HandleTypeDef *huart);
void gyro_rx_callback(UART_HandleTypeDef *huart, uint16_t size);
void gyro_error_callback(UART_HandleTypeDef *huart);
void gyro_task(void);
void gyro_on_camera_frame(void);
float gyro_get_delta_yaw(void);
gyro_data_t gyro_get_data(void);

#endif
