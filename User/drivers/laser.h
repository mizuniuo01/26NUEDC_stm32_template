#ifndef DRIVER_LIBRARY_ST_DRIVERS_LASER_H
#define DRIVER_LIBRARY_ST_DRIVERS_LASER_H

#include <stdint.h>
#include <stm32f4xx_hal.h>

/* 激光配置结构体 */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t active_level; /* 有效电平：1=高电平点亮，0=低电平点亮 */
} laser_cfg_t;

/* 激光句柄 */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t active_level;
} laser_handle_t;

void laser_init(laser_handle_t *handle, const laser_cfg_t *cfg);
void laser_on(laser_handle_t *handle);
void laser_off(laser_handle_t *handle);
void laser_toggle(laser_handle_t *handle);

#endif /* DRIVER_LIBRARY_ST_DRIVERS_LASER_H */
