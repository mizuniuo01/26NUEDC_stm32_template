#ifndef SYSTEM_H
#define SYSTEM_H

#include <stm32f4xx_hal.h>

/* 驱动头文件（只添加有对应句柄的） */
#include "led.h"
#include "buzzer.h"
#include "bldc.h"
#include "motor.h"
#include "pid.h"
#include "motion_control.h"

/* GPIO 控制类 */
led_handle_t *system_led1(void);
led_handle_t *system_led2(void);
led_handle_t *system_led3(void);
led_handle_t *system_led4(void);
buzzer_handle_t *system_buzzer(void);

/* 驱动 / 执行器类 */
motor_handle_t *system_motor(void);

/* 获取 F32C 电机共享串口总线实例，调用方不得直接修改其成员。 */
bldc_bus_t *system_bldc_bus(void);

/* 获取地址为 1 的云台 X 轴 F32C 电机实例。 */
bldc_motor_t *system_bldc_x(void);

/* 获取地址为 2 的云台 Y 轴 F32C 电机实例。 */
bldc_motor_t *system_bldc_y(void);

/* 算法类 */
pid_t *system_pid_speed_left(void);
pid_t *system_pid_speed_right(void);
pid_t *system_pid_angle(void);

void system_init(void);
void set_system_led_flag(uint8_t state);
void system_state(void);

#endif /* SYSTEM_H */
