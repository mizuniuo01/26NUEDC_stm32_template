#ifndef SENSOR_H
#define SENSOR_H

#include <stdint.h>
#include <stm32f4xx_hal.h>

#define SENSOR_I2C_ADDR_7BIT 0x4C                   /* 感为科技八路灰度 I2C 7 位地址 */
#define SENSOR_I2C_ADDR (SENSOR_I2C_ADDR_7BIT << 1) /* 左移 1 位地址 */
#define SENSOR_CMD_READ_DIG 0xDD                    /* 读取数字量命令 */

/* 传感器 DMA 状态 */
typedef enum {
    SENSOR_STATE_IDLE = 0,
    SENSOR_STATE_BUSY,
} sensor_state_t;

extern volatile uint8_t sensor_tick_flag;

void sensor_init(I2C_HandleTypeDef *hi2c);
sensor_state_t sensor_get_state(void);
void sensor_request_dma(void);
void sensor_rx_callback(I2C_HandleTypeDef *hi2c);
void sensor_error_callback(I2C_HandleTypeDef *hi2c);
uint8_t sensor_read_data(void);
void sensor_task(void);

#endif
