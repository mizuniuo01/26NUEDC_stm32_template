#ifndef PWM_H
#define PWM_H

#include <stdint.h>
#include <stm32f4xx_hal.h>

/*
 * PWM_MAX_COMPARE 对应 CubeMX 中定时器的 ARR 值（自动重装载）。
 * 若修改 CubeMX 中的定时器时钟或 ARR，需同步更新此宏。
 */

#define PWM_MAX_COMPARE 8400 /* 定时器 ARR 值，决定 PWM 分辨率 */

void pwm_init(TIM_HandleTypeDef *htim);
void pwm_set_compare_ch1(TIM_HandleTypeDef *htim, uint16_t compare);
void pwm_set_compare_ch2(TIM_HandleTypeDef *htim, uint16_t compare);

#endif
