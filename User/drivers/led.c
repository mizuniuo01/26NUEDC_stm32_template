/**
 * @file    led.c
 * @brief   LED 驱动模块（GPIO 控制）
 * @author  mizuniuo01
 * @date    2026-05-25
 * @version 1.0.0
 * @note    依赖：GPIO 已在 CubeMX 中配置
 * @note    参数非法时通过 error_report(ERROR_SOURCE_LED, DRV_ERR_PARAM) 上报
 *
 * @usage
 * 驱动层模块，负责单一 GPIO 的亮灭控制。多实例设计，每个 LED
 * 对应一个独立的 handle，通过 active_level 适配高低电平两种接法。
 *
 * 基本用法：
 *
 * static led_handle_t led1;
 * static led_handle_t led2;
 *
 * void system_init(void)
 * {
 *     led_cfg_t cfg1 = { .port = LED1_GPIO_Port,
 *                        .pin  = LED1_Pin,
 *                        .active_level = 1 };
 *     led_cfg_t cfg2 = { .port = LED2_GPIO_Port,
 *                        .pin  = LED2_Pin,
 *                        .active_level = 0 };
 *     led_init(&led1, &cfg1);
 *     led_init(&led2, &cfg2);
 * }
 *
 * led_on(&led1);
 * led_off(&led2);
 * led_toggle(&led1);
 * led_set(&led2, flag);   // flag=0 熄灭，非0 点亮
 *
 * 闪烁等时序相关模式请在应用层用状态机 + tick 实现。
 *
 * 跨文件共享句柄时，通过项目 system.h/c 的 getter 获取指针，
 * 禁止在其他文件中创建同名 static 句柄（CODING_STANDARD.md §12.8.1）。
 */

#include "led.h"
#include "error_handler.h"

/**
 * @brief  LED 初始化
 * @param  handle  LED 句柄指针
 * @param  cfg     LED 配置指针
 * @retval 无
 */
void led_init(led_handle_t *handle, const led_cfg_t *cfg)
{
    if (!handle || !cfg) {
        error_report(ERROR_SOURCE_LED, DRV_ERR_PARAM);
        return;
    }

    handle->port = cfg->port;
    handle->pin = cfg->pin;
    handle->active_level = cfg->active_level;

    /* 初始状态：关闭 */
    led_off(handle);
}

/**
 * @brief  点亮 LED
 * @param  handle  LED 句柄指针
 * @retval 无
 */
void led_on(led_handle_t *handle)
{
    if (!handle) {
        error_report(ERROR_SOURCE_LED, DRV_ERR_PARAM);
        return;
    }

    if (handle->active_level) {
        HAL_GPIO_WritePin(handle->port, handle->pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(handle->port, handle->pin, GPIO_PIN_RESET);
    }
}

/**
 * @brief  熄灭 LED
 * @param  handle  LED 句柄指针
 * @retval 无
 */
void led_off(led_handle_t *handle)
{
    if (!handle) {
        error_report(ERROR_SOURCE_LED, DRV_ERR_PARAM);
        return;
    }

    if (handle->active_level) {
        HAL_GPIO_WritePin(handle->port, handle->pin, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(handle->port, handle->pin, GPIO_PIN_SET);
    }
}

/**
 * @brief  翻转 LED 状态
 * @param  handle  LED 句柄指针
 * @retval 无
 */
void led_toggle(led_handle_t *handle)
{
    if (!handle) {
        error_report(ERROR_SOURCE_LED, DRV_ERR_PARAM);
        return;
    }

    HAL_GPIO_TogglePin(handle->port, handle->pin);
}

/**
 * @brief  设置 LED 状态
 * @param  handle  LED 句柄指针
 * @param  state   0=熄灭，非0=点亮
 * @retval 无
 */
void led_set(led_handle_t *handle, uint8_t state)
{
    if (!handle) {
        error_report(ERROR_SOURCE_LED, DRV_ERR_PARAM);
        return;
    }

    if (state) {
        led_on(handle);
    } else {
        led_off(handle);
    }
}
