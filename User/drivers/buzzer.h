#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>
#include <stm32f4xx_hal.h>

/* 蜂鸣器配置结构体 */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t active_level; /* 有效电平：1=高电平响，0=低电平响 */
} buzzer_cfg_t;

/* 蜂鸣器句柄 */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t active_level;
} buzzer_handle_t;

void buzzer_init(buzzer_handle_t *handle, const buzzer_cfg_t *cfg);
void buzzer_on(buzzer_handle_t *handle);
void buzzer_off(buzzer_handle_t *handle);
void buzzer_toggle(buzzer_handle_t *handle);

#endif
