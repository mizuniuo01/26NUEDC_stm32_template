/**
 * @file    buzzer.c
 * @brief   蜂鸣器驱动模块（GPIO 控制）
 * @author  mizuniuo01
 * @date    2026-05-25
 * @version 1.0.0
 * @note    依赖：GPIO 已在 CubeMX 中配置
 * @note    参数非法时通过 error_report(ERROR_SOURCE_BUZZER, DRV_ERR_PARAM) 上报
 *
 * @usage
 * 驱动层模块，负责单一 GPIO 的开关控制，不管理时序。多实例设计，
 * 引脚通过 buzzer_init() 注入，不依赖具体硬件宏。
 *
 * 基本用法：
 *
 * static buzzer_handle_t buzzer;
 *
 * void system_init(void)
 * {
 *     buzzer_cfg_t cfg = { .port = Buzzer_GPIO_Port,
 *                          .pin  = Buzzer_Pin,
 *                          .active_level = 1 };
 *     buzzer_init(&buzzer, &cfg);
 * }
 *
 * buzzer_on(&buzzer);
 * buzzer_off(&buzzer);
 * buzzer_toggle(&buzzer);
 *
 * 非阻塞鸣叫模式请在应用层用状态机 + tick 实现，
 * buzzer 模块不提供定时自动关闭功能。
 *
 * 跨文件共享句柄时，通过项目 system.h/c 的 getter 获取指针，
 * 禁止在其他文件中创建同名 static 句柄（CODING_STANDARD.md §12.8.1）。
 */

#include "buzzer.h"
#include "error_handler.h"

/**
 * @brief  蜂鸣器初始化
 * @param  handle  蜂鸣器句柄指针
 * @param  cfg     蜂鸣器配置指针
 * @retval 无
 */
void buzzer_init(buzzer_handle_t *handle, const buzzer_cfg_t *cfg)
{
    if (!handle || !cfg) {
        error_report(ERROR_SOURCE_BUZZER, DRV_ERR_PARAM);
        return;
    }

    handle->port = cfg->port;
    handle->pin = cfg->pin;

    handle->active_level = cfg->active_level;
 
    /* 初始关闭 */
    buzzer_off(handle);
}

/**
 * @brief  打开蜂鸣器
 * @param  handle  蜂鸣器句柄指针
 * @retval 无
 */
void buzzer_on(buzzer_handle_t *handle)
{
    if (!handle) {
        error_report(ERROR_SOURCE_BUZZER, DRV_ERR_PARAM);
        return;
    }

    if (handle->active_level) {
        HAL_GPIO_WritePin(handle->port, handle->pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(handle->port, handle->pin, GPIO_PIN_RESET);
    }
}

/**
 * @brief  关闭蜂鸣器
 * @param  handle  蜂鸣器句柄指针
 * @retval 无
 */
void buzzer_off(buzzer_handle_t *handle)
{
    if (!handle) {
        error_report(ERROR_SOURCE_BUZZER, DRV_ERR_PARAM);
        return;
    }

    if (handle->active_level) {
        HAL_GPIO_WritePin(handle->port, handle->pin, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(handle->port, handle->pin, GPIO_PIN_SET);
    }
}

/**
 * @brief  翻转蜂鸣器状态
 * @param  handle  蜂鸣器句柄指针
 * @retval 无
 */
void buzzer_toggle(buzzer_handle_t *handle)
{
    if (!handle) {
        error_report(ERROR_SOURCE_BUZZER, DRV_ERR_PARAM);
        return;
    }

    HAL_GPIO_TogglePin(handle->port, handle->pin);
}
