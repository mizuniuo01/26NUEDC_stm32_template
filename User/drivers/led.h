#ifndef LED_H
#define LED_H

#include <stdint.h>
#include <stm32f4xx_hal.h>

/* LED 配置结构体 */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t active_level; /* 有效电平：1=高电平点亮，0=低电平点亮 */
} led_cfg_t;

/* LED 句柄 */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t active_level;
} led_handle_t;

void led_init(led_handle_t *handle, const led_cfg_t *cfg);
void led_on(led_handle_t *handle);
void led_off(led_handle_t *handle);
void led_toggle(led_handle_t *handle);
void led_set(led_handle_t *handle, uint8_t state);

#endif
