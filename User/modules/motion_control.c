/**
 * @file    motion_control.c
 * @brief   底层闭环控制模块（角度环 + 双轮速度环）
 * @author  mizuniuo01
 * @date    2026-07-15
 * @version 1.0.0
 * @note    只做稳定跟踪，不做任何决策与规划
 * @note    10ms 周期：TIM6 ISR 置 motion_control_tick_flag，主循环调 motion_control_task
 * @note    反馈来源：encoder_get_left/right()（count/10ms）、gyro_get_data().yaw
 * @note    yaw 使用 unwrap 累积到无边界空间，target 同步映射到最近等效值，
 *          避免 pid_calc 内部 error 计算跨 ±180° 时突变，同时保留微分-on-actual 的作用
 * @note    PID 句柄通过 system.c 注册对外暴露（左右速度 PID 共享参数）
 * @warning 角度环使能状态下才叠加 angle_diff；external_diff 始终生效
 *
 * @usage
 * ─────────────────────────────────────────────────────────
 * 三通道输入 + 一个使能开关，task 内部按 10ms 节拍完成：
 *
 *   yaw_unwrapped += wrap(gyro.yaw - yaw_last_raw)
 *   angle_diff  = angle_enable ? angle_pid(target_unwrapped, yaw_unwrapped) : 0
 *   total_diff  = angle_diff + external_diff
 *   left_pwm  = speed_pid_l(base - total_diff, encoder_left)
 *   right_pwm = speed_pid_r(base + total_diff, encoder_right)
 *   motor_set_speed_left/right(pwm)
 *
 * PID 参数调试：通过 system_pid_xxx() 获取句柄后调 pid_set_param/pid_get_param。
 * 左右速度共享参数，调用方需同步刷新两个句柄。
 *
 * 跨文件共享句柄时，通过项目 system.h/c 的 getter 获取指针，
 * 禁止在其他文件中创建同名 static 句柄（CODING_STANDARD.md §12.8.1）。
 */

#include "motion_control.h"
#include "encoder.h"
#include "gyroscope.h"
#include "motor.h"
#include "system.h"
#include "tim.h"

volatile uint8_t motion_control_tick_flag = 0;

/* PID 实例（左右速度独立句柄，共享参数） */
static pid_t pid_speed_left;
static pid_t pid_speed_right;
static pid_t pid_angle;

/* 运动输入通道 */
static int16_t base_speed;
static int16_t external_diff;
static float target_angle_deg;
static uint8_t angle_enable;

/* yaw unwrap 状态：把 [-180,180] 环绕域累积成无边界连续量，供 pid_calc 消费 */
static float yaw_last_raw;      /* 上一周期陀螺仪原始 yaw（[-180, 180]） */
static float yaw_unwrapped;     /* 累积到无边界空间的 yaw */
static uint8_t yaw_unwrap_init; /* 首次进入 task 时初始化累积器 */

/**
 * @brief  将角度归一化到 [-180, 180]
 * @param  angle  输入角度（度）
 * @retval 归一化后的角度
 */
static float mc_wrap_180(float angle)
{
    while (angle > 180.0f) {
        angle -= 360.0f;
    }
    while (angle < -180.0f) {
        angle += 360.0f;
    }
    return angle;
}

/**
 * @brief  更新 yaw 累积值（消除 ±180° 环绕）
 * @param  yaw_raw  陀螺仪原始 yaw（[-180, 180]）
 * @retval 累积后的无边界 yaw
 */
static float mc_yaw_unwrap(float yaw_raw)
{
    float delta;

    if (!yaw_unwrap_init) {
        yaw_unwrap_init = 1;
        yaw_last_raw = yaw_raw;
        yaw_unwrapped = yaw_raw;
        return yaw_unwrapped;
    }

    delta = mc_wrap_180(yaw_raw - yaw_last_raw);
    yaw_unwrapped += delta;
    yaw_last_raw = yaw_raw;
    return yaw_unwrapped;
}

/**
 * @brief  将目标角度映射到 yaw_unwrapped 附近的最近等效值
 * @note   避免 target 与 yaw_unwrapped 之间跨多圈时误差爆炸
 * @param  target_deg     用户设定的目标角度（度）
 * @param  yaw_unwrapped  当前累积 yaw
 * @retval 映射后的目标角（与 yaw_unwrapped 在同一累积空间内）
 */
static float mc_target_align(float target_deg, float yaw_unwrapped_now)
{
    float diff = mc_wrap_180(target_deg - yaw_unwrapped_now);
    return yaw_unwrapped_now + diff;
}

/**
 * @brief  对称限幅
 * @param  value  输入值
 * @param  limit  正的对称上限
 * @retval 限幅后的值
 */
static int16_t mc_clamp_i16(int32_t value, int32_t limit)
{
    if (value > limit) {
        return (int16_t)limit;
    }
    if (value < -limit) {
        return (int16_t)(-limit);
    }
    return (int16_t)value;
}

/**
 * @brief  motion_control 初始化：三路 PID + 输入通道清零
 * @param  无
 * @retval 无
 */
void motion_control_init(void)
{
    pid_init(&pid_speed_left, MC_SPEED_PID_KP, MC_SPEED_PID_KI, MC_SPEED_PID_KD,
        MC_SPEED_PID_OUT_MAX, MC_SPEED_PID_INTEGRAL_MAX);
    pid_init(&pid_speed_right, MC_SPEED_PID_KP, MC_SPEED_PID_KI, MC_SPEED_PID_KD,
        MC_SPEED_PID_OUT_MAX, MC_SPEED_PID_INTEGRAL_MAX);
    pid_init(&pid_angle, MC_ANGLE_PID_KP, MC_ANGLE_PID_KI, MC_ANGLE_PID_KD,
        MC_ANGLE_PID_OUT_MAX, MC_ANGLE_PID_INTEGRAL_MAX);

    base_speed = 0;
    external_diff = 0;
    target_angle_deg = 0.0f;
    angle_enable = 0;

    yaw_last_raw = 0.0f;
    yaw_unwrapped = 0.0f;
    yaw_unwrap_init = 0;
}

/**
 * @brief  motion_control 主任务（10ms 周期，上层调用）
 * @note   tick_flag 由 TIM6 ISR 置位
 * @param  无
 * @retval 无
 */
void motion_control_task(void)
{
    int16_t encoder_left;
    int16_t encoder_right;
    gyro_data_t gyro;
    float yaw_abs;
    float target_abs;
    float angle_diff_f;
    int16_t angle_diff;
    int16_t total_diff;
    int16_t left_target;
    int16_t right_target;
    float left_pwm_f;
    float right_pwm_f;

    if (!motion_control_tick_flag) {
        return;
    }
    motion_control_tick_flag = 0;

    /* 反馈采样 */
    encoder_left = encoder_get_left();
    encoder_right = encoder_get_right();

    /* 角度环：使能时输出差速，禁用时清零并保持 PID 积分不累积 */
    if (angle_enable) {
        gyro = gyro_get_data();
        yaw_abs = mc_yaw_unwrap(gyro.yaw);
        target_abs = mc_target_align(target_angle_deg, yaw_abs);
        angle_diff_f = pid_calc(&pid_angle, target_abs, yaw_abs);
        angle_diff = mc_clamp_i16((int32_t)angle_diff_f, MOTION_CONTROL_DIFF_MAX);
    } else {
        pid_clear(&pid_angle);
        angle_diff = 0;
    }

    /* 差速叠加：外部差速始终生效 */
    total_diff = mc_clamp_i16((int32_t)angle_diff + (int32_t)external_diff,
        MOTION_CONTROL_DIFF_MAX);

    /* 左右目标 count/10ms */
    left_target = mc_clamp_i16((int32_t)base_speed - (int32_t)total_diff,
        MOTION_CONTROL_TARGET_MAX);
    right_target = mc_clamp_i16((int32_t)base_speed + (int32_t)total_diff,
        MOTION_CONTROL_TARGET_MAX);

    /* 速度环双轮独立闭环 */
    left_pwm_f = pid_calc(&pid_speed_left, (float)left_target, (float)encoder_left);
    right_pwm_f = pid_calc(&pid_speed_right, (float)right_target, (float)encoder_right);

    motor_set_speed_left(system_motor(), &htim3, (int16_t)left_pwm_f);
    motor_set_speed_right(system_motor(), &htim3, (int16_t)right_pwm_f);
}

/**
 * @brief  设置基础速度（左右轮共用）
 * @param  base  count/10ms，正值前进，负值后退
 * @retval 无
 */
void motion_control_set_base_speed(int16_t base)
{
    base_speed = mc_clamp_i16((int32_t)base, MOTION_CONTROL_TARGET_MAX);
}

/**
 * @brief  设置角度环目标（绝对角度锁定）
 * @param  target_deg  目标 yaw（度）
 * @retval 无
 */
void motion_control_set_angle(float target_deg)
{
    target_angle_deg = target_deg;
}

/**
 * @brief  设置外部差速（视觉巡线等）
 * @param  diff  count/10ms，正值向左偏，负值向右偏
 * @retval 无
 */
void motion_control_set_diff(int16_t diff)
{
    external_diff = mc_clamp_i16((int32_t)diff, MOTION_CONTROL_DIFF_MAX);
}

/**
 * @brief  角度环使能开关
 * @param  enable  非零启用，0 禁用
 * @retval 无
 */
void motion_control_enable_angle(uint8_t enable)
{
    if (enable && !angle_enable) {
        pid_clear(&pid_angle);
        yaw_unwrap_init = 0; /* 重启 unwrap 累积，避免旧漂移带入 */
    }
    angle_enable = enable ? 1 : 0;
}

/**
 * @brief  获取左轮速度 PID 句柄
 * @param  无
 * @retval PID 句柄指针
 */
pid_t *motion_control_pid_speed_left(void)
{
    return &pid_speed_left;
}

/**
 * @brief  获取右轮速度 PID 句柄
 * @param  无
 * @retval PID 句柄指针
 */
pid_t *motion_control_pid_speed_right(void)
{
    return &pid_speed_right;
}

/**
 * @brief  获取角度环 PID 句柄
 * @param  无
 * @retval PID 句柄指针
 */
pid_t *motion_control_pid_angle(void)
{
    return &pid_angle;
}

/**
 * @brief  获取角度环目标指针
 * @param  无
 * @retval 目标角度指针
 */
float *motion_control_get_target_angle_ptr(void)
{
    return &target_angle_deg;
}
