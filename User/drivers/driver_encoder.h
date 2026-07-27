#ifndef AUTO_BALL_CAR_USER_DRIVERS_DRIVER_ENCODER_H
#define AUTO_BALL_CAR_USER_DRIVERS_DRIVER_ENCODER_H /* 头文件保护 */

#include "status.h"
#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

/* 定时器正交编码器配置 */
typedef struct {
    TIM_HandleTypeDef *timer; /* CubeMX 生成的编码器定时器句柄 */
    int8_t sign;              /* 安装方向修正符号，取值为 -1 或 1 */
    uint8_t counter_bits;     /* 定时器计数器有效位数 */
} driver_encoder_config_t;

/* 定时器正交编码器实例 */
typedef struct {
    driver_encoder_config_t config; /* 配置快照 */
    uint32_t last_count;            /* 上一次采样的定时器计数值 */
    int32_t delta;                  /* 最近一个周期的有符号计数增量 */
    bool is_initialized;            /* 驱动初始化完成标志 */
} driver_encoder_t;

/* 生命周期、周期处理与数据查询接口 */
status_code_t driver_encoder_init(driver_encoder_t *encoder, const driver_encoder_config_t *config);
status_code_t driver_encoder_process(driver_encoder_t *encoder);
int32_t driver_encoder_delta(const driver_encoder_t *encoder);

#endif /* AUTO_BALL_CAR_USER_DRIVERS_DRIVER_ENCODER_H */
