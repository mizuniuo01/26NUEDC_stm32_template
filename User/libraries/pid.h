#ifndef PID_H
#define PID_H

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
    float kp;
    float ki;
    float kd;

    float target;
    float actual;

    float error;
    float error_last;
    float actual_last;
    float integral;

    float out;
    float out_max;
    float out_min;
    float integral_max;
} pid_t;

void pid_init(pid_t *pid, float p, float i, float d, float out_max, float integral_max);
void pid_set_param(pid_t *pid, const pid_param_t *param);
void pid_get_param(const pid_t *pid, pid_param_t *param);
float pid_calc(pid_t *pid, float target, float actual);
void pid_clear(pid_t *pid);

#endif
