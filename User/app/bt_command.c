/**
 * @file    bt_command.c
 * @brief   蓝牙指令回调实现（get → 加减步长 → set）
 * @author  mizuniuo01
 * @date    2026-07-15
 * @version 1.0.0
 */

#include "bt_command.h"
#include "system.h"
#include "led.h"
#include "pid.h"
#include "motion_control.h"
#include "motion_manager.h"

/* ==================== 速度 PID ==================== */

/**
 * @brief  速度环 KP 增大
 * @param  无
 * @retval 无
 */
void on_spd_kp_up(void)
{
    pid_param_t p;
    pid_get_param(system_pid_speed_left(), &p);
    p.kp += BLT_STEP_SPD_KP;
    pid_set_param(system_pid_speed_left(), &p);
    pid_set_param(system_pid_speed_right(), &p);
}

/**
 * @brief  速度环 KP 减小
 * @param  无
 * @retval 无
 */
void on_spd_kp_down(void)
{
    pid_param_t p;
    pid_get_param(system_pid_speed_left(), &p);
    p.kp -= BLT_STEP_SPD_KP;
    pid_set_param(system_pid_speed_left(), &p);
    pid_set_param(system_pid_speed_right(), &p);
}

/**
 * @brief  速度环 KI 增大
 * @param  无
 * @retval 无
 */
void on_spd_ki_up(void)
{
    pid_param_t p;
    pid_get_param(system_pid_speed_left(), &p);
    p.ki += BLT_STEP_SPD_KI;
    pid_set_param(system_pid_speed_left(), &p);
    pid_set_param(system_pid_speed_right(), &p);
}

/**
 * @brief  速度环 KI 减小
 * @param  无
 * @retval 无
 */
void on_spd_ki_down(void)
{
    pid_param_t p;
    pid_get_param(system_pid_speed_left(), &p);
    p.ki -= BLT_STEP_SPD_KI;
    pid_set_param(system_pid_speed_left(), &p);
    pid_set_param(system_pid_speed_right(), &p);
}

/**
 * @brief  速度环 KD 增大
 * @param  无
 * @retval 无
 */
void on_spd_kd_up(void)
{
    pid_param_t p;
    pid_get_param(system_pid_speed_left(), &p);
    p.kd += BLT_STEP_SPD_KD;
    pid_set_param(system_pid_speed_left(), &p);
    pid_set_param(system_pid_speed_right(), &p);
}

/**
 * @brief  速度环 KD 减小
 * @param  无
 * @retval 无
 */
void on_spd_kd_down(void)
{
    pid_param_t p;
    pid_get_param(system_pid_speed_left(), &p);
    p.kd -= BLT_STEP_SPD_KD;
    pid_set_param(system_pid_speed_left(), &p);
    pid_set_param(system_pid_speed_right(), &p);
}

/* ==================== 循迹运动参数 ==================== */

/**
 * @brief  调整指定档位的循迹差速。
 * @param  level  差速档位。
 * @param  delta  有符号调整量，单位为 count/10ms。
 * @return 无。
 */
static void adjust_motion_diff(motion_manager_diff_level_t level, int16_t delta)
{
    int16_t diff;

    diff = motion_manager_get_diff(level);
    motion_manager_set_diff(level, (int16_t)(diff + delta));
}

/**
 * @brief  增大循迹基础速度。
 * @return 无。
 */
void on_base_spd_up(void)
{
    int16_t base_speed = motion_manager_get_base_speed();

    motion_manager_set_base_speed((int16_t)(base_speed + BLT_STEP_BASE_SPD));
}

/**
 * @brief  减小循迹基础速度。
 * @return 无。
 */
void on_base_spd_down(void)
{
    int16_t base_speed = motion_manager_get_base_speed();

    motion_manager_set_base_speed((int16_t)(base_speed - BLT_STEP_BASE_SPD));
}

/**
 * @brief  增大微偏差速。
 * @return 无。
 */
void on_small_diff_up(void)
{
    adjust_motion_diff(MOTION_MANAGER_DIFF_LEVEL_SMALL, BLT_STEP_DIFF);
}

/**
 * @brief  减小微偏差速。
 * @return 无。
 */
void on_small_diff_down(void)
{
    adjust_motion_diff(MOTION_MANAGER_DIFF_LEVEL_SMALL, -BLT_STEP_DIFF);
}

/**
 * @brief  增大中偏差速。
 * @return 无。
 */
void on_medium_diff_up(void)
{
    adjust_motion_diff(MOTION_MANAGER_DIFF_LEVEL_MEDIUM, BLT_STEP_DIFF);
}

/**
 * @brief  减小中偏差速。
 * @return 无。
 */
void on_medium_diff_down(void)
{
    adjust_motion_diff(MOTION_MANAGER_DIFF_LEVEL_MEDIUM, -BLT_STEP_DIFF);
}

/**
 * @brief  增大大偏差速。
 * @return 无。
 */
void on_big_diff_up(void)
{
    adjust_motion_diff(MOTION_MANAGER_DIFF_LEVEL_BIG, BLT_STEP_DIFF);
}

/**
 * @brief  减小大偏差速。
 * @return 无。
 */
void on_big_diff_down(void)
{
    adjust_motion_diff(MOTION_MANAGER_DIFF_LEVEL_BIG, -BLT_STEP_DIFF);
}

/**
 * @brief  启动循迹运动。
 * @return 无。
 */
void on_motion_start(void)
{
    motion_manager_start();
}

/**
 * @brief  停止循迹运动。
 * @return 无。
 */
void on_motion_stop(void)
{
    motion_manager_stop();
}

/* ==================== 目标角度（直接操作 motion_control 共享指针） ==================== */

/**
 * @brief  目标角度增大
 * @param  无
 * @retval 无
 */
void on_target_ang_up(void)
{
    float *ptr = motion_control_get_target_angle_ptr();
    *ptr += BLT_STEP_TARGET_ANG;
    motion_control_set_angle(*ptr);
}

/**
 * @brief  目标角度减小
 * @param  无
 * @retval 无
 */
void on_target_ang_down(void)
{
    float *ptr = motion_control_get_target_angle_ptr();
    *ptr -= BLT_STEP_TARGET_ANG;
    motion_control_set_angle(*ptr);
}
