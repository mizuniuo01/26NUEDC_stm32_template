/**
 * @file    pid.c
 * @brief   PID 控制器模块（微分-on-实际值）
 * @note    纯算法模块，无硬件依赖
 * @note    微分项作用在实际值上，避免目标突变导致微分尖峰
 * @note    含积分饱和限幅和输出限幅
 * @note    参数非法时安全返回，不产生跨层错误上报副作用
 *
 * @usage
 * pid_t pid;
 * pid_init(&pid, 1.5f, 0.1f, 0.05f, 500.0f, 200.0f);
 * float out = pid_calc(&pid, target, actual);
 * pid_clear(&pid);
 *
 * PID 是纯逻辑对象，由所属服务或领域对象直接持有，不依赖 BSP 句柄。
 */

#include "pid.h"

/**
 * @brief  PID 初始化
 * @param  pid           PID 句柄指针
 * @param  p             比例系数
 * @param  i             积分系数
 * @param  d             微分系数
 * @param  out_max       输出限幅
 * @param  integral_max  积分饱和限幅
 */
void pid_init(pid_t *pid, float p, float i, float d, float out_max, float integral_max)
{
    if (!pid) {
        return;
    }

    pid->kp = p;
    pid->ki = i;
    pid->kd = d;

    pid->target = 0.0f;
    pid->actual = 0.0f;
    pid->error = 0.0f;
    pid->error_last = 0.0f;
    pid->actual_last = 0.0f;
    pid->integral = 0.0f;

    pid->out = 0.0f;
    pid->out_max = out_max;
    pid->out_min = -out_max;
    pid->integral_max = integral_max;
}

/**
 * @brief  更新 PID 参数，不清空历史状态
 * @param  pid    PID 句柄指针
 * @param  param  参数集指针
 */
void pid_set_param(pid_t *pid, const pid_param_t *param)
{
    if (!pid || !param) {
        return;
    }

    pid->kp = param->kp;
    pid->ki = param->ki;
    pid->kd = param->kd;
    pid->out_max = param->out_max;
    pid->out_min = -param->out_max;
    pid->integral_max = param->integral_max;
}

/**
 * @brief  读取 PID 当前参数
 * @param  pid    PID 句柄指针
 * @param  param  参数集输出指针
 */
void pid_get_param(const pid_t *pid, pid_param_t *param)
{
    if (!pid || !param) {
        return;
    }

    param->kp = pid->kp;
    param->ki = pid->ki;
    param->kd = pid->kd;
    param->out_max = pid->out_max;
    param->integral_max = pid->integral_max;
}

/**
 * @brief  PID 计算（微分-on-实际值）
 * @param  pid     PID 句柄指针
 * @param  target  目标值
 * @param  actual  实际值
 * @return PID 输出值
 */
float pid_calc(pid_t *pid, float target, float actual)
{
    if (!pid) {
        return 0.0f;
    }

    pid->target = target;
    pid->actual = actual;

    /* 计算本次误差 */
    pid->error = pid->target - pid->actual;

    /* 积分项累加 */
    pid->integral += pid->error;

    /* 积分饱和限幅 */
    if (pid->integral > pid->integral_max) {
        pid->integral = pid->integral_max;
    } else if (pid->integral < -pid->integral_max) {
        pid->integral = -pid->integral_max;
    }

    /* 微分-on-实际值，避免目标突变尖峰 */
    pid->out = pid->kp * pid->error + pid->ki * pid->integral +
               pid->kd * (pid->actual_last - pid->actual);

    pid->error_last = pid->error;
    pid->actual_last = pid->actual;

    /* 输出限幅 */
    if (pid->out > pid->out_max) {
        pid->out = pid->out_max;
    } else if (pid->out < pid->out_min) {
        pid->out = pid->out_min;
    }

    return pid->out;
}

/**
 * @brief  清除 PID 历史状态
 * @param  pid  PID 句柄指针
 */
void pid_clear(pid_t *pid)
{
    if (!pid) {
        return;
    }

    pid->error = 0.0f;
    pid->error_last = 0.0f;
    pid->actual_last = 0.0f;
    pid->integral = 0.0f;
    pid->out = 0.0f;
}
