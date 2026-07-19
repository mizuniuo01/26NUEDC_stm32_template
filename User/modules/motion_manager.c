/**
 * @file    motion_manager.c
 * @brief   运动控制管理层（普通闭环 + 运动规划）
 * @author  mizuniuo01
 * @date    2026-07-15
 * @version 1.0.0
 * @note    对内调用 motion_control，对上层提供统一运动接口
 * @note    10ms tick：普通闭环透传，运动规划推进距离/角度判定
 * @note    PLANNING 模式下 set_normal 无效，由规划内部全权控制
 * @warning start_move/start_rotate 为异步接口，即时返回，task 中推进
 *
 * @usage
 * ─────────────────────────────────────────────────────────
 *
 * ── 普通闭环 ──
 * motion_manager_set_normal(speed, angle, diff, enable_angle);
 * // 内部调 motion_control_set_base_speed/angle/diff + enable_angle
 *
 * ── 运动规划 ──
 * motion_manager_start_move(500, 50);   // 前进 500mm，速度 50 count/10ms
 * motion_manager_start_rotate(90, 30);  // 相对初始角度左转 90°，速度 30
 *
 * motion_manager_state_t s = motion_manager_get_state();
 * // 轮询状态：NORMAL/MOVE/ROTATE
 *
 * 跨文件共享句柄时，通过项目 system.h/c 的 getter 获取指针，
 * 禁止在其他文件中创建同名 static 句柄（CODING_STANDARD.md §12.8.1）。
 */

#include "motion_manager.h"
#include "motion_control.h"
#include "encoder.h"
#include "gyroscope.h"
#include "system.h"

/* 编码器→距离换算常量（依赖 encoder_cfg_t 枚举值，无法用宏） */
static const float counts_per_output_rev =
    (float)(ENCODER_PPR * ENCODER_MULTIPLIER * GEAR_RATIO);
static const float counts_per_mm = counts_per_output_rev / WHEEL_CIRCUMFERENCE_MM;

volatile uint8_t motion_manager_tick_flag = 0;

/* 状态 */
static motion_manager_state_t state;

/* NORMAL 模式参数缓存 */
static int16_t normal_base_speed;
static float normal_target_angle;
static int16_t normal_external_diff;
static uint8_t normal_angle_enable;

/* 初始偏航角（上电记录，旋转规划基准） */
static float home_yaw;

/* MOVE 规划 */
static float target_total_counts;
static float accumulated_counts;
static int16_t plan_distance_mm; /* 目标距离，供外部查询 */

/* ROTATE 规划 */
static float rotate_target_deg;
static int16_t rotate_speed;

/* 前一周期 encoder raw，用于判断是否首次进入 tick */
static uint8_t move_first_tick;

/**
 * @brief  角度环绕：归一到 [-180, 180]
 * @param  angle  输入角度（度）
 * @retval 归一化后的角度（[-180, 180]）
 */
static float mgr_wrap_180(float angle)
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
 * @brief  motion_manager 初始化
 * @param  无
 * @retval 无
 */
void motion_manager_init(void)
{
    gyro_data_t gyro;

    state = MOTION_MANAGER_STATE_NORMAL;
    gyro = gyro_get_data();
    home_yaw = gyro.yaw;

    normal_base_speed = 0;
    normal_target_angle = home_yaw;
    normal_external_diff = 0;
    normal_angle_enable = 1;

    /* 上电即锁定初始角度 */
    motion_control_set_angle(home_yaw);
    motion_control_enable_angle(1);
}

/**
 * @brief  普通闭环统一接口（仅在 NORMAL 状态下生效）
 * @param  base_speed     基础速度（count/10ms）
 * @param  target_angle   角度目标（度）
 * @param  external_diff  外部差速（count/10ms）
 * @param  angle_enable   角度环使能（非零启用）
 * @retval 无
 */
void motion_manager_set_normal(int16_t base_speed, float target_angle,
    int16_t external_diff, uint8_t angle_enable)
{
    normal_base_speed = base_speed;
    normal_target_angle = target_angle;
    normal_external_diff = external_diff;
    normal_angle_enable = angle_enable;

    if (state != MOTION_MANAGER_STATE_NORMAL) {
        return;
    }

    motion_control_set_base_speed(base_speed);
    motion_control_set_angle(target_angle);
    motion_control_set_diff(external_diff);
    motion_control_enable_angle(angle_enable);
}

/**
 * @brief  启动移动距离规划（异步，即时返回）
 * @param  distance_mm  目标距离（mm）
 * @param  speed        行驶速度（count/10ms）
 * @retval 无
 */
void motion_manager_start_move(int16_t distance_mm, int16_t speed)
{
    if (distance_mm <= 0) {
        return;
    }

    target_total_counts = (float)distance_mm * counts_per_mm;
    accumulated_counts = 0.0f;
    move_first_tick = 1;
    plan_distance_mm = distance_mm;

    motion_control_set_base_speed(speed);
    motion_control_set_diff(0);
    motion_control_enable_angle(1);

    state = MOTION_MANAGER_STATE_MOVE;
}

/**
 * @brief  启动旋转角度规划（异步，即时返回）
 * @note   基准为 home_yaw（上电记录角度），非当前角度
 * @param  delta_deg  角度增量（正值左转，负值右转，度）
 * @param  speed      行驶速度（count/10ms）
 * @retval 无
 */
void motion_manager_start_rotate(float delta_deg, int16_t speed)
{
    rotate_target_deg = *motion_control_get_target_angle_ptr() + delta_deg;
    rotate_speed = speed;

    motion_control_set_base_speed(speed);
    motion_control_set_angle(rotate_target_deg);
    motion_control_set_diff(0);
    motion_control_enable_angle(1);

    state = MOTION_MANAGER_STATE_ROTATE;
}

/**
 * @brief  motion_manager 主任务（10ms 周期，主循环调用）
 * @param  无
 * @retval 无
 */
void motion_manager_task(void)
{
    int16_t enc_l;
    int16_t enc_r;
    float avg_count;
    float yaw_now;
    float yaw_error;

    if (!motion_manager_tick_flag) {
        return;
    }
    motion_manager_tick_flag = 0;

    switch (state) {
        case MOTION_MANAGER_STATE_NORMAL:
            break;

        case MOTION_MANAGER_STATE_MOVE:
            enc_l = encoder_get_left();
            enc_r = encoder_get_right();

            if (enc_l < 0) {
                enc_l = (int16_t)(-enc_l);
            }
            if (enc_r < 0) {
                enc_r = (int16_t)(-enc_r);
            }

            /* 首次 tick 跳过（10ms 计数尚未稳定） */
            if (move_first_tick) {
                move_first_tick = 0;
                break;
            }

            avg_count = (float)(enc_l + enc_r) * ENCODER_AVG_FACTOR;
            accumulated_counts += avg_count;

            if (accumulated_counts >= target_total_counts) {
                /* done：恢复 start 前的 NORMAL 参数，避免速度跳变 */
                pid_clear(system_pid_speed_left());
                pid_clear(system_pid_speed_right());
                motion_manager_set_normal(normal_base_speed, normal_target_angle,
                    normal_external_diff, normal_angle_enable);
                state = MOTION_MANAGER_STATE_NORMAL;
            }
            break;

        case MOTION_MANAGER_STATE_ROTATE:
            yaw_now = gyro_get_data().yaw;
            yaw_error = mgr_wrap_180(rotate_target_deg - yaw_now);

            if (yaw_error < 0.0f) {
                yaw_error = -yaw_error;
            }

            if (yaw_error <= ROTATE_DEAD_ZONE_DEG) {
                /* done：恢复速度，保持角度锁定 */
                pid_clear(system_pid_speed_left());
                pid_clear(system_pid_speed_right());
                motion_control_set_base_speed(normal_base_speed);
                motion_control_set_diff(normal_external_diff);
                /* 角度和目标保持 rotate 设置的值不变 */
                state = MOTION_MANAGER_STATE_NORMAL;
            }
            break;

        default:
            state = MOTION_MANAGER_STATE_NORMAL;
            break;
    }
}

/**
 * @brief  查询当前状态
 * @param  无
 * @retval 当前状态枚举
 */
motion_manager_state_t motion_manager_get_state(void)
{
    return state;
}

/**
 * @brief  获取当前距离规划已走距离
 * @param  无
 * @retval 已走距离（mm），非 MOVE 状态返回 0
 */
int16_t motion_manager_get_elapsed_mm(void)
{
    if (state != MOTION_MANAGER_STATE_MOVE) {
        return 0;
    }
    return (int16_t)(accumulated_counts / counts_per_mm);
}

/**
 * @brief  获取当前距离规划剩余距离
 * @param  无
 * @retval 剩余距离（mm），非 MOVE 状态返回 0
 */
int16_t motion_manager_get_remaining_mm(void)
{
    float remaining;

    if (state != MOTION_MANAGER_STATE_MOVE) {
        return 0;
    }
    remaining = (target_total_counts - accumulated_counts) / counts_per_mm;
    if (remaining < 0.0f) {
        remaining = 0.0f;
    }
    return (int16_t)remaining;
}

/**
 * @brief  取消当前运动规划，立即停车
 * @note   清除速度 PID，速度归零，diff 归零，返回 NORMAL 状态
 * @param  无
 * @retval 无
 */
void motion_manager_cancel(void)
{
    if (state == MOTION_MANAGER_STATE_NORMAL) {
        return;
    }

    pid_clear(system_pid_speed_left());
    pid_clear(system_pid_speed_right());
    motion_control_set_base_speed(0);
    motion_control_set_diff(0);
    normal_base_speed = 0;
    normal_external_diff = 0;
    state = MOTION_MANAGER_STATE_NORMAL;
}

/**
 * @brief  保持当前速度，调整剩余目标距离
 * @note   仅在 MOVE 状态下有效，不改变速度，仅调整目标计数
 * @param  remaining_mm  新的剩余距离（mm）
 * @retval 无
 */
void motion_manager_replan_remaining_mm(int16_t remaining_mm)
{
    float remaining_counts;

    if (state != MOTION_MANAGER_STATE_MOVE) {
        return;
    }
    if (remaining_mm <= 0) {
        return;
    }

    remaining_counts = (float)remaining_mm * counts_per_mm;
    target_total_counts = accumulated_counts + remaining_counts;
    plan_distance_mm = remaining_mm;
}

/**
 * @brief  角度锁定：读当前 yaw 并设为目标 + enable_angle
 * @note   用于 control_manager 初始化或状态切换时锁定当前朝向
 * @param  无
 * @retval 无
 */
void motion_manager_lock_angle(void)
{
    gyro_data_t gyro = gyro_get_data();

    *motion_control_get_target_angle_ptr() = gyro.yaw;
    motion_control_set_angle(gyro.yaw);
    motion_control_enable_angle(1);
}

/**
 * @brief  透传 motion_control_task（control_manager 禁止直接调 motion_control）
 * @param  无
 * @retval 无
 */
void motion_manager_run_control_task(void)
{
    motion_control_task();
}

/**
 * @brief  立即停车（不取消运动规划，仅清零速度/diff）
 * @param  无
 * @retval 无
 */
void motion_manager_halt(void)
{
    motion_control_set_base_speed(0);
    motion_control_set_diff(0);
}

/**
 * @brief  停车并保持角度锁定
 * @param  无
 * @retval 无
 */
void motion_manager_hold_stop(void)
{
    motion_control_set_base_speed(0);
    motion_control_set_diff(0);
    motion_control_enable_angle(1);
}
