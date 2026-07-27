#ifndef AUTO_BALL_CAR_USER_DRIVERS_DRIVER_ULTRASONIC_H
#define AUTO_BALL_CAR_USER_DRIVERS_DRIVER_ULTRASONIC_H /* 头文件保护 */

#include "status.h"
#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

/* HC-SR04 非阻塞测距配置 */
typedef struct {
    TIM_HandleTypeDef *timer;    /* 回波输入捕获定时器句柄 */
    uint32_t channel;            /* 回波输入捕获定时器通道 */
    GPIO_TypeDef *trigger_port;  /* 触发信号 GPIO 端口 */
    uint16_t trigger_pin;        /* 触发信号 GPIO 引脚 */
    uint32_t trigger_period_ms;  /* 两次测距触发的最小间隔，单位：毫秒 */
} driver_ultrasonic_config_t;

/* HC-SR04 非阻塞测距驱动实例 */
/* 输入捕获回调写 volatile 测距结果，主循环只读取最近完成的快照。 */
typedef struct {
    driver_ultrasonic_config_t config; /* 配置快照 */
    volatile uint32_t rising_capture;  /* 回波上升沿捕获计数 */
    volatile uint16_t distance_mm;     /* 最近一次有效距离，单位：毫米 */
    volatile bool is_waiting_for_fall; /* 等待回波下降沿标志 */
    volatile bool is_valid;            /* 最近一次距离有效标志 */
    uint32_t last_trigger_ms;           /* 最近一次触发时间，单位：毫秒 */
    bool is_initialized;               /* 驱动初始化完成标志 */
} driver_ultrasonic_t;

/* 生命周期与非阻塞测距推进接口 */
status_code_t driver_ultrasonic_init(driver_ultrasonic_t *ultrasonic,
    const driver_ultrasonic_config_t *config);
status_code_t driver_ultrasonic_process(driver_ultrasonic_t *ultrasonic, uint32_t now_ms);

/* HAL 输入捕获回调转发接口 */
void driver_ultrasonic_capture_isr(driver_ultrasonic_t *ultrasonic, TIM_HandleTypeDef *timer,
    uint32_t channel);

/* 最近一次测距结果查询接口 */
status_code_t driver_ultrasonic_read(const driver_ultrasonic_t *ultrasonic, uint16_t *distance_mm,
    bool *is_valid);

#endif /* AUTO_BALL_CAR_USER_DRIVERS_DRIVER_ULTRASONIC_H */
