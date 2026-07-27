#ifndef AUTO_BALL_CAR_USER_LIBRARIES_PID_H
#define AUTO_BALL_CAR_USER_LIBRARIES_PID_H /* 头文件保护 */

#include <stdint.h>

/* PID 参数集（用于批量读写） */
typedef struct {
    float kp;           /* 比例系数 */
    float ki;           /* 积分系数 */
    float kd;           /* 微分系数 */
    float out_max;      /* 输出上限（对称限幅） */
    float integral_max; /* 积分饱和限幅（对称） */
} pid_param_t;

/* PID 控制器 */
typedef struct {
    float kp; /* 比例系数 */
    float ki; /* 积分系数 */
    float kd; /* 微分系数 */

    float target; /* 目标值 */
    float actual; /* 实际值 */

    float error;       /* 当前误差 */
    float error_last;  /* 上一次误差 */
    float actual_last; /* 上一次实际值（微分-on-实际值） */
    float integral;    /* 积分累加 */

    float out;          /* PID 输出 */
    float out_max;      /* 输出上限 */
    float out_min;      /* 输出下限 */
    float integral_max; /* 积分限幅 */
} pid_t;

/* 生命周期与参数接口 */
void pid_init(pid_t *pid, float kp, float ki, float kd, float out_max, float integral_max);
void pid_set_param(pid_t *pid, const pid_param_t *param);
void pid_get_param(const pid_t *pid, pid_param_t *param);

/* 控制计算与动态状态接口 */
float pid_calc(pid_t *pid, float target, float actual);
void pid_clear(pid_t *pid);

#endif /* AUTO_BALL_CAR_USER_LIBRARIES_PID_H */
