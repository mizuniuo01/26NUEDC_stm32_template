/**
 * @file driver_keys.c
 * @brief 采集并消抖固定数量的 GPIO 按键输入。
 */
#include "driver_keys.h"
#include <string.h>

/**
 * @brief  读取全部按键的原始语义状态
 * @param  keys 已配置引脚的按键驱动实例
 * @return 按键按下状态位掩码
 */
static uint8_t read_state(const driver_keys_t *keys)
{
    uint8_t state = 0U;
    uint8_t i;

    for (i = 0U; i < DRIVER_KEYS_COUNT; i++) {
        if (HAL_GPIO_ReadPin(keys->pins[i].port, keys->pins[i].pin) == keys->pins[i].active_level) {
            state |= (uint8_t)(1U << i);
        }
    }
    return state;
}

/**
 * @brief  初始化按键驱动并采集初始稳定状态
 * @param  keys 按键驱动实例
 * @param  pins 包含 DRIVER_KEYS_COUNT 项的按键引脚配置
 * @param  debounce_ms 状态保持稳定后生效的时间，单位：毫秒
 * @retval STATUS_OK 按键驱动已初始化
 * @retval STATUS_INVALID_ARGUMENT keys 或 pins 为空
 */
status_code_t driver_keys_init(driver_keys_t *keys, const driver_key_pin_t *pins,
    uint32_t debounce_ms)
{
    if (!keys || !pins) {
        return STATUS_INVALID_ARGUMENT;
    }
    (void)memcpy(keys->pins, pins, sizeof(keys->pins));
    keys->stable_state = read_state(keys);
    keys->candidate_state = keys->stable_state;
    keys->pressed_events = 0U;
    keys->changed_at_ms = 0U;
    keys->debounce_ms = debounce_ms;
    keys->is_initialized = true;
    return STATUS_OK;
}

/**
 * @brief  采样一次按键并推进非阻塞消抖状态
 * @param  keys 按键驱动实例
 * @param  now_ms 当前单调时间，单位：毫秒
 * @retval STATUS_OK 按键状态已处理
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 */
status_code_t driver_keys_process(driver_keys_t *keys, uint32_t now_ms)
{
    uint8_t current;

    if (!keys || !keys->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    current = read_state(keys);
    if (current != keys->stable_state) {
        if (current != keys->candidate_state) {
            keys->candidate_state = current;
            keys->changed_at_ms = now_ms;
        } else if ((uint32_t)(now_ms - keys->changed_at_ms) >= keys->debounce_ms) {
            keys->pressed_events |= (uint8_t)(current & (uint8_t)~keys->stable_state);
            keys->stable_state = current;
        }
    } else {
        keys->candidate_state = current;
    }
    return STATUS_OK;
}

/**
 * @brief  读取已经消抖的按键稳定状态
 * @param  keys 按键驱动实例
 * @return 按键稳定状态位掩码，keys 为空时返回零
 */
uint8_t driver_keys_state(const driver_keys_t *keys)
{
    return keys ? keys->stable_state : 0U;
}

/**
 * @brief  读取并清除尚未处理的按键按下事件
 * @param  keys 按键驱动实例
 * @return 按下事件位掩码，keys 为空时返回零
 */
uint8_t driver_keys_take_pressed(driver_keys_t *keys)
{
    uint8_t events;

    if (!keys) {
        return 0U;
    }
    events = keys->pressed_events;
    keys->pressed_events = 0U;
    return events;
}
