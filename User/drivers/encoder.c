/**
 * @file    encoder.c
 * @brief   编码器驱动模块（双 QEI 模式）
 * @author  mizuniuo01
 * @date    2026-05-25
 * @version 1.0.0
 * @note    左右轮均使用 STM32 硬件 QEI 模式，方向由硬件自动判定
 * @note    encoder_scan_left / encoder_scan_right 为 B 类操作，ISR 中直接调用
 * @note    与 TI 版不同：无需 motor 模块提供方向标志位，无需捕获 ISR
 * @note    参数非法时通过 error_report(ERROR_SOURCE_ENCODER, DRV_ERR_PARAM) 上报
 *
 * @usage
 * ─────────────────────────────────────────────────────────
 * 适配两路编码电机方案，与 pwm、motor 模块配合使用。
 *
 * 硬件：左右编码器各连接一个定时器的 CH1/CH2（QEI 模式）。
 * CubeMX 中将两个定时器均配置为 Encoder Mode。
 *
 * ── 初始化 ──
 *
 * encoder_init(&htim2, &htim3);
 *
 * ── ISR 中周期性扫描（B 类）──
 *
 * encoder_scan_left(&htim2);
 * encoder_scan_right(&htim3);
 *
 * ── 数据读取 ──
 *
 * int16_t left  = encoder_get_left();
 * int16_t right = encoder_get_right();
 */

#include "encoder.h"
#include "error_handler.h"

static int16_t encoder_left_val;
static int16_t encoder_right_val;

/**
 * @brief  编码器初始化（双 QEI 定时器）
 * @param  htim_left   左编码器定时器句柄
 * @param  htim_right  右编码器定时器句柄
 * @retval 无
 */
void encoder_init(TIM_HandleTypeDef *htim_left, TIM_HandleTypeDef *htim_right)
{
    if (!htim_left || !htim_right) {
        error_report(ERROR_SOURCE_ENCODER, DRV_ERR_PARAM);
        return;
    }

    __HAL_TIM_SET_COUNTER(htim_left, 0);
    __HAL_TIM_SET_COUNTER(htim_right, 0);

    HAL_TIM_Encoder_Start(htim_left, TIM_CHANNEL_ALL);
    HAL_TIM_Encoder_Start(htim_right, TIM_CHANNEL_ALL);
}

/**
 * @brief  左编码器扫描（B 类，ISR 中直接调用）
 * @note   利用无符号转有符号的截断机制，自动处理定时器溢出
 * @param  htim  左编码器定时器句柄
 * @retval 无
 */
void encoder_scan_left(TIM_HandleTypeDef *htim)
{
    static uint16_t last_count_left;
    uint16_t current_count;
    int16_t diff_value;

    if (!htim) {
        error_report(ERROR_SOURCE_ENCODER, DRV_ERR_PARAM);
        return;
    }

    current_count = __HAL_TIM_GET_COUNTER(htim);
    diff_value = (int16_t)(current_count - last_count_left);

    /* 取反适配安装方向 */
    encoder_left_val = -diff_value;
    last_count_left = current_count;
}

/**
 * @brief  右编码器扫描（B 类，ISR 中直接调用）
 * @param  htim  右编码器定时器句柄
 * @retval 无
 */
void encoder_scan_right(TIM_HandleTypeDef *htim)
{
    static uint16_t last_count_right;
    uint16_t current_count;
    int16_t diff_value;

    if (!htim) {
        error_report(ERROR_SOURCE_ENCODER, DRV_ERR_PARAM);
        return;
    }

    current_count = __HAL_TIM_GET_COUNTER(htim);
    diff_value = (int16_t)(current_count - last_count_right);

    encoder_right_val = diff_value;
    last_count_right = current_count;
}

/**
 * @brief  获取左编码器值
 * @param  无
 * @retval 左轮当前增量
 */
int16_t encoder_get_left(void)
{
    return encoder_left_val;
}

/**
 * @brief  获取右编码器值
 * @param  无
 * @retval 右轮当前增量
 */
int16_t encoder_get_right(void)
{
    return encoder_right_val;
}
