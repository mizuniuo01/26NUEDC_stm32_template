#ifndef ENCODER_H
#define ENCODER_H

#include "tim.h"
#include <stdint.h>
#include <stm32f4xx_hal.h>

void encoder_init(TIM_HandleTypeDef *htim_left, TIM_HandleTypeDef *htim_right);
void encoder_scan_left(TIM_HandleTypeDef *htim);
void encoder_scan_right(TIM_HandleTypeDef *htim);
int16_t encoder_get_left(void);
int16_t encoder_get_right(void);

#endif
