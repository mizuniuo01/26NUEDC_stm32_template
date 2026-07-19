#ifndef MOTION_MANAGER_H
#define MOTION_MANAGER_H

#include <stdint.h>

/* 编码器参数 */
typedef enum {
    ENCODER_PPR = 13,
    ENCODER_MULTIPLIER = 4,   /* QEI 四倍频 */
    GEAR_RATIO = 28,
} encoder_cfg_t;

/* 双轮编码器平均系数 */
#define ENCODER_AVG_FACTOR 0.5f

/* 轮胎与运动参数 */
#define WHEEL_DIAMETER_MM 65.0f        /* 轮胎直径（mm） */
#define WHEEL_CIRCUMFERENCE_MM (3.1415926f * WHEEL_DIAMETER_MM) /* 轮周长（mm） */
#define ROTATE_DEAD_ZONE_DEG 3.0f         /* 角度到达死区（度） */

/* 运动管理层状态 */
typedef enum {
    MOTION_MANAGER_STATE_NORMAL = 0, /* 普通闭环 */
    MOTION_MANAGER_STATE_MOVE,       /* 运动规划-距离 */
    MOTION_MANAGER_STATE_ROTATE,     /* 运动规划-角度 */
} motion_manager_state_t;

extern volatile uint8_t motion_manager_tick_flag;

void motion_manager_init(void);
void motion_manager_task(void);

/* 普通闭环 —— 统一接口，内部分发给 motion_control */
void motion_manager_set_normal(int16_t base_speed, float target_angle_deg,
    int16_t external_diff, uint8_t angle_enable);

/* 运动规划 —— 异步发起，task 内推进 */
void motion_manager_start_move(int16_t distance_mm, int16_t speed);
void motion_manager_start_rotate(float delta_deg, int16_t speed);

/* 查询当前状态 */
motion_manager_state_t motion_manager_get_state(void);

/* 距离规划辅助 */
int16_t motion_manager_get_elapsed_mm(void);
int16_t motion_manager_get_remaining_mm(void);
void motion_manager_cancel(void);
void motion_manager_replan_remaining_mm(int16_t remaining_mm);

/* 透传接口（control_manager 禁止直接调 motion_control） */
void motion_manager_lock_angle(void);
void motion_manager_run_control_task(void);
void motion_manager_halt(void);
void motion_manager_hold_stop(void);

#endif
