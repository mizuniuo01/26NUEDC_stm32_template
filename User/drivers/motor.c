/**
 * @file    motor.c
 * @brief   直流有刷电机驱动模块（DRV8874 IN/IN 模式，两路 PWM）
 * @author  mizuniuo01
 * @date    2026-05-25
 * @version 1.0.0
 * @note    左电机使用 PWM CH1（PA6），右电机使用 PWM CH2（PA7）
 * @note    左右电机方向引脚逻辑相反（机械安装方向导致）
 * @note    依赖：PWM 模块（pwm_set_compare_ch1/ch2）
 * @warning 参数 speed 为直接 PWM 比较值，非物理速度
 * @note    参数非法时通过 error_report(ERROR_SOURCE_MOTOR, DRV_ERR_PARAM) 上报
 *
 * @usage
 * ─────────────────────────────────────────────────────────
 * 适配两路直流有刷电机 + 两片 DRV8874 驱动芯片。
 *
 * ── 硬件拓扑（DRV8874 PH/EN 模式）──
 *
 *   MCU TIM3 CH1 (PA6) ──→ DRV8874#1 EN  (PWM)
 *   MCU GPIO dirl (PA5) ──→ DRV8874#1 PH  (方向)
 *   MCU GPIO sleepl (PA4) → DRV8874#1 nSLEEP
 *   DRV8874#1 OUT1/OUT2 ──→ 左电机
 *
 *   MCU TIM3 CH2 (PA7) ──→ DRV8874#2 EN  (PWM)
 *   MCU GPIO dirr (PC4) ──→ DRV8874#2 PH  (方向)
 *   MCU GPIO sleepr (PC5) → DRV8874#2 nSLEEP
 *   DRV8874#2 OUT1/OUT2 ──→ 右电机
 *
 * 左右电机机械对向安装，方向引脚逻辑相反：
 * - 左电机正向 = PH2 置高
 * - 右电机正向 = PH2 清零
 *
 * ── 初始化 ──
 *
 * static motor_handle_t motor;
 *
 * motor_cfg_t cfg = {
 *     .l_port       = GPIOA,
 *     .r_port       = GPIOC,
 *     .l_nsleep_pin = sleepl_Pin,
 *     .r_nsleep_pin = sleepr_Pin,
 *     .l_ph_pin     = dirl_Pin,
 *     .r_ph_pin     = dirr_Pin,
 * };
 * motor_init(&motor, &cfg);
 *
 * ── 速度控制 ──
 *
 * // PWM 量程 -8400 ~ +8400，符号决定方向，绝对值决定占空比
 * motor_set_speed_left(&motor, &htim1,  4200);  // 左轮 50%
 * motor_set_speed_right(&motor, &htim1, -6300);  // 右轮 75% 反转
 *
 * // 低于 MOTOR_EFFECTIVE_MIN_SPEED(252) 的值自动输出 0
 *
 * ── 闭环控制链路 ──
 *
 *   encoder_get_left/right()  →  PID  →  motor_set_speed_left/right()
 *        ↑                                  │
 *   encoder_scan_left/right()               ↓
 *        ↑                          pwm_set_compare_ch1/ch2()
 *   [QEI 编码器]                          ↓
 *                                  [DRV8874 → 电机]
 *
 * 跨文件共享句柄时，通过项目 system.h/c 的 getter 获取指针，
 * 禁止在其他文件中创建同名 static 句柄（CODING_STANDARD.md §12.8.1）。
 */

#include "motor.h"
#include "error_handler.h"
#include "pwm.h"

/**
 * @brief  电机初始化（唤醒 DRV8874）
 * @param  handle  电机句柄指针
 * @param  cfg     电机配置指针
 * @retval 无
 */
void motor_init(motor_handle_t *handle, const motor_cfg_t *cfg)
{
    if (!handle || !cfg) {
        error_report(ERROR_SOURCE_MOTOR, DRV_ERR_PARAM);
        return;
    }

    handle->l_port = cfg->l_port;
    handle->r_port = cfg->r_port;
    handle->l_nsleep_pin = cfg->l_nsleep_pin;
    handle->r_nsleep_pin = cfg->r_nsleep_pin;
    handle->l_ph_pin = cfg->l_ph_pin;
    handle->r_ph_pin = cfg->r_ph_pin;

    /* 拉高 nSLEEP 使驱动芯片脱离待机模式 */
    HAL_GPIO_WritePin(handle->l_port, handle->l_nsleep_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(handle->r_port, handle->r_nsleep_pin, GPIO_PIN_SET);
}

/**
 * @brief  设置左电机速度
 * @param  handle  电机句柄指针
 * @param  htim    PWM 定时器句柄
 * @param  speed   PWM 比较值（-PWM_MAX_COMPARE ~ +PWM_MAX_COMPARE）
 * @retval 无
 */
void motor_set_speed_left(motor_handle_t *handle, TIM_HandleTypeDef *htim, int16_t speed)
{
    int16_t temp;

    if (!handle || !htim) {
        error_report(ERROR_SOURCE_MOTOR, DRV_ERR_PARAM);
        return;
    }

    if (speed >= MOTOR_MIN_SPEED) {
        if (speed > MOTOR_MAX_SPEED) {
            speed = MOTOR_MAX_SPEED;
        }

        if (speed < MOTOR_EFFECTIVE_MIN_SPEED) {
            pwm_set_compare_ch1(htim, 0);
            return;
        }

        HAL_GPIO_WritePin(handle->l_port, handle->l_ph_pin, GPIO_PIN_SET);
        pwm_set_compare_ch1(htim, (uint16_t)speed);
    } else {
        temp = -speed;
        if (temp > MOTOR_MAX_SPEED) {
            temp = MOTOR_MAX_SPEED;
        }

        if (temp < MOTOR_EFFECTIVE_MIN_SPEED) {
            pwm_set_compare_ch1(htim, 0);
            return;
        }

        HAL_GPIO_WritePin(handle->l_port, handle->l_ph_pin, GPIO_PIN_RESET);
        pwm_set_compare_ch1(htim, (uint16_t)temp);
    }
}

/**
 * @brief  设置右电机速度
 * @note   方向引脚极性与左电机相反
 * @param  handle  电机句柄指针
 * @param  htim    PWM 定时器句柄
 * @param  speed   PWM 比较值（-PWM_MAX_COMPARE ~ +PWM_MAX_COMPARE）
 * @retval 无
 */
void motor_set_speed_right(motor_handle_t *handle, TIM_HandleTypeDef *htim, int16_t speed)
{
    int16_t temp;

    if (!handle || !htim) {
        error_report(ERROR_SOURCE_MOTOR, DRV_ERR_PARAM);
        return;
    }

    if (speed >= MOTOR_MIN_SPEED) {
        if (speed > MOTOR_MAX_SPEED) {
            speed = MOTOR_MAX_SPEED;
        }

        if (speed < MOTOR_EFFECTIVE_MIN_SPEED) {
            pwm_set_compare_ch2(htim, 0);
            return;
        }

        HAL_GPIO_WritePin(handle->r_port, handle->r_ph_pin, GPIO_PIN_RESET);
        pwm_set_compare_ch2(htim, (uint16_t)speed);
    } else {
        temp = -speed;
        if (temp > MOTOR_MAX_SPEED) {
            temp = MOTOR_MAX_SPEED;
        }

        if (temp < MOTOR_EFFECTIVE_MIN_SPEED) {
            pwm_set_compare_ch2(htim, 0);
            return;
        }

        HAL_GPIO_WritePin(handle->r_port, handle->r_ph_pin, GPIO_PIN_SET);
        pwm_set_compare_ch2(htim, (uint16_t)temp);
    }
}
