/**
 * @file    laser.c
 * @brief   激光驱动模块（GPIO 控制）
 * @note    依赖：GPIO 已在 CubeMX 中配置
 * @note    参数非法时通过 error_report(ERROR_SOURCE_LASER, DRV_ERR_PARAM) 上报
 *
 * @usage
 * static laser_handle_t laser;
 *
 * laser_cfg_t cfg = { .port = GPIOD,
 *                     .pin = GPIO_PIN_11,
 *                     .active_level = 1 };
 * laser_init(&laser, &cfg);
 * laser_on(&laser);
 * laser_off(&laser);
 * laser_toggle(&laser);
 *
 * 跨文件共享句柄时，通过项目 system.h/c 的 getter 获取指针，
 * 禁止在其他文件中创建同名 static 句柄（ARCHITECTURE_STANDARD.md）。
 */

#include "laser.h"
#include "error_handler.h"

/**
 * @brief  激光初始化
 * @param  handle  激光句柄指针
 * @param  cfg     激光配置指针
 */
void laser_init(laser_handle_t *handle, const laser_cfg_t *cfg)
{
    if (!handle || !cfg) {
        error_report(ERROR_SOURCE_LASER, DRV_ERR_PARAM);
        return;
    }

    handle->port = cfg->port;
    handle->pin = cfg->pin;
    handle->active_level = cfg->active_level;

    /* 初始关闭 */
    laser_off(handle);
}

/**
 * @brief  打开激光
 * @param  handle  激光句柄指针
 */
void laser_on(laser_handle_t *handle)
{
    if (!handle) {
        error_report(ERROR_SOURCE_LASER, DRV_ERR_PARAM);
        return;
    }

    if (handle->active_level) {
        HAL_GPIO_WritePin(handle->port, handle->pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(handle->port, handle->pin, GPIO_PIN_RESET);
    }
}

/**
 * @brief  关闭激光
 * @param  handle  激光句柄指针
 */
void laser_off(laser_handle_t *handle)
{
    if (!handle) {
        error_report(ERROR_SOURCE_LASER, DRV_ERR_PARAM);
        return;
    }

    if (handle->active_level) {
        HAL_GPIO_WritePin(handle->port, handle->pin, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(handle->port, handle->pin, GPIO_PIN_SET);
    }
}

/**
 * @brief  翻转激光状态
 * @param  handle  激光句柄指针
 */
void laser_toggle(laser_handle_t *handle)
{
    if (!handle) {
        error_report(ERROR_SOURCE_LASER, DRV_ERR_PARAM);
        return;
    }

    HAL_GPIO_TogglePin(handle->port, handle->pin);
}
