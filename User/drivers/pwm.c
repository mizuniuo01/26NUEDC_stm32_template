/**
 * @file    pwm.c
 * @brief   PWM 输出模块（定时器 CH3/CH4 比较值控制）
 * @author  mizuniuo01
 * @date    2026-05-25
 * @version 1.0.0
 * @note    依赖：PWM 定时器已在 CubeMX 中配置
 * @note    使用 CH1/CH2 通道，适配 STM32 通用定时器
 * @note    高级定时器（TIM1/TIM8）额外使能 MOE 主输出
 * @note    参数非法时通过 error_report(ERROR_SOURCE_PWM, DRV_ERR_PARAM) 上报
 *
 * @usage
 * ─────────────────────────────────────────────────────────
 * PWM 比较值薄封装，供 motor 模块调用。
 *
 * CubeMX 配置要点：
 * - 定时器 CH3/CH4 设为 PWM Generation
 * - ARR 值决定 PWM_MAX_COMPARE（当前为 8400）
 * - 若为高级定时器（TIM1/TIM8），需在 CubeMX 中使能 MOE
 *
 * ── 初始化 ──
 *
 * pwm_init(&htim1);  // 启动 CH1/CH2 PWM 输出
 *
 * ── motor 模块调用 ──
 *
 * pwm_set_compare_ch1(&htim1, 4200);  // 50% 占空比
 */

#include "pwm.h"
#include "error_handler.h"

/**
 * @brief  启动 PWM 通道输出
 * @note   高级定时器额外使能 MOE 和互补输出
 * @param  htim  定时器句柄指针
 * @retval 无
 */
void pwm_init(TIM_HandleTypeDef *htim)
{
    if (!htim) {
        error_report(ERROR_SOURCE_PWM, DRV_ERR_PARAM);
        return;
    }

    HAL_TIM_PWM_Start(htim, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(htim, TIM_CHANNEL_2);
}

/**
 * @brief  设置 PWM 通道 1 的比较值（占空比）
 * @param  htim     定时器句柄指针
 * @param  compare  比较值（0~PWM_MAX_COMPARE），超限自动钳位
 * @retval 无
 */
void pwm_set_compare_ch1(TIM_HandleTypeDef *htim, uint16_t compare)
{
    if (!htim) {
        error_report(ERROR_SOURCE_PWM, DRV_ERR_PARAM);
        return;
    }

    if (compare > PWM_MAX_COMPARE) {
        compare = PWM_MAX_COMPARE;
    }

    __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_1, compare);
}

/**
 * @brief  设置 PWM 通道 2 的比较值（占空比）
 * @param  htim     定时器句柄指针
 * @param  compare  比较值（0~PWM_MAX_COMPARE），超限自动钳位
 * @retval 无
 */
void pwm_set_compare_ch2(TIM_HandleTypeDef *htim, uint16_t compare)
{
    if (!htim) {
        error_report(ERROR_SOURCE_PWM, DRV_ERR_PARAM);
        return;
    }

    if (compare > PWM_MAX_COMPARE) {
        compare = PWM_MAX_COMPARE;
    }

    __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_2, compare);
}
