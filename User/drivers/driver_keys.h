#ifndef AUTO_BALL_CAR_USER_DRIVERS_DRIVER_KEYS_H
#define AUTO_BALL_CAR_USER_DRIVERS_DRIVER_KEYS_H /* 头文件保护 */

#include "status.h"
#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

#define DRIVER_KEYS_COUNT 5U /* 板载按键数量 */

/* 一个具有语义有效电平的按键输入 */
typedef struct {
    GPIO_TypeDef *port;         /* GPIO 端口 */
    uint16_t pin;               /* HAL GPIO 引脚位掩码 */
    GPIO_PinState active_level; /* 按键按下时读取到的电平 */
} driver_key_pin_t;

/* 按键消抖器状态 */
typedef struct {
    driver_key_pin_t pins[DRIVER_KEYS_COUNT]; /* 按键输入配置副本 */
    uint8_t stable_state;                    /* 已消抖的按键状态位掩码 */
    uint8_t candidate_state;                 /* 等待确认的候选状态位掩码 */
    uint8_t pressed_events;                  /* 尚未取走的按下事件位掩码 */
    bool is_initialized;                     /* 驱动初始化完成标志 */
    uint32_t changed_at_ms;                  /* 候选状态开始时间，单位：毫秒 */
    uint32_t debounce_ms;                    /* 消抖时间，单位：毫秒 */
} driver_keys_t;

/* 生命周期、周期处理与状态查询接口 */
status_code_t driver_keys_init(driver_keys_t *keys, const driver_key_pin_t *pins,
    uint32_t debounce_ms);
status_code_t driver_keys_process(driver_keys_t *keys, uint32_t now_ms);
uint8_t driver_keys_state(const driver_keys_t *keys);
uint8_t driver_keys_take_pressed(driver_keys_t *keys);

#endif /* AUTO_BALL_CAR_USER_DRIVERS_DRIVER_KEYS_H */
