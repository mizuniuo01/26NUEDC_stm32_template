#ifndef MOTION_CONTROL_H
#define MOTION_CONTROL_H

#include "pid.h"
#include <stdint.h>

/* 目标与差速的对称限幅（count/10ms） */
typedef enum {
    MOTION_CONTROL_TARGET_MAX = 100,  /* 编码器 count/10ms 上限（约等于 100% 占空比） */
    MOTION_CONTROL_DIFF_MAX = 100,  /* 单侧差速上限，left/right 目标最终再限幅 */
} motion_control_limit_t;

/* 速度环 PID 默认参数（编码器 count/10ms → PWM 比较值 0~8400） */
#define MC_SPEED_PID_KP 40.0f   /* 速度环比例系数 */
#define MC_SPEED_PID_KI 2.0f    /* 速度环积分系数 */
#define MC_SPEED_PID_KD 0.0f    /* 速度环微分系数 */
#define MC_SPEED_PID_OUT_MAX 8400.0f /* 速度环输出上限 */
#define MC_SPEED_PID_INTEGRAL_MAX 2000.0f /* 速度环积分上限 */

/* 角度环 PID 默认参数（yaw 误差 → 差速 count/10ms） */
#define MC_ANGLE_PID_KP 1.0f    /* 角度环比例系数 */
#define MC_ANGLE_PID_KI 0.0f    /* 角度环积分系数 */
#define MC_ANGLE_PID_KD 13.0f    /* 角度环微分系数 */
#define MC_ANGLE_PID_OUT_MAX 50.0f   /* 角度环输出上限 */
#define MC_ANGLE_PID_INTEGRAL_MAX 10.0f  /* 角度环积分上限 */

/* motion_control 任务节拍（TIM6 ISR 置1，主循环消费） */
extern volatile uint8_t motion_control_tick_flag;

void motion_control_init(void);
void motion_control_task(void);

/* 运动输入 —— 三通道 */
void motion_control_set_base_speed(int16_t base_speed);
void motion_control_set_angle(float target_angle_deg);
void motion_control_set_diff(int16_t external_diff);

/* 角度环使能开关（禁用时 angle_diff = 0） */
void motion_control_enable_angle(uint8_t enable);

/* 内部 PID 句柄导出，供 system.c 注册给上层调参 */
pid_t *motion_control_pid_speed_left(void);
pid_t *motion_control_pid_speed_right(void);
pid_t *motion_control_pid_angle(void);

/* 目标角度指针（唯一数据源，上层直接读写） */
float *motion_control_get_target_angle_ptr(void);

#endif
