/**
 * @file    motion_manager.c
 * @brief   基于八路灰度传感器的循迹运动管理模块。
 * @author  mizuniuo01
 * @date    2026-07-21
 * @version 2.0.0
 * @note    本模块只负责循迹策略：读取传感器、识别 Pattern、选择三档差速。
 * @note    底层仅使用左右轮速度闭环，角度数据和角度闭环不属于本模块职责。
 * @note    编译期宏只提供上电默认值，运行参数由 setter/getter 持有和调整。
 * @note    motion_manager_task() 由 TIM6 的 10ms 标志驱动，并在主循环中执行。
 *
 * @usage
 * motion_manager_set_base_speed(30);
 * motion_manager_set_diff(MOTION_MANAGER_DIFF_LEVEL_SMALL, 5);
 * motion_manager_start();
 *
 * while (1) {
 *     sensor_task();
 *     motion_manager_task();
 *     motion_control_task();
 * }
 */

#include "motion_manager.h"
#include "motion_control.h"
#include "pattern.h"
#include "sensor.h"

/** @brief 上电默认基础速度，单位为 count/10ms。 */
#define MOTION_MANAGER_DEFAULT_BASE_SPEED 30

/** @brief 上电默认微偏差速，单位为 count/10ms。 */
#define MOTION_MANAGER_DEFAULT_SMALL_DIFF 5

/** @brief 上电默认中偏差速，单位为 count/10ms。 */
#define MOTION_MANAGER_DEFAULT_MEDIUM_DIFF 6

/** @brief 上电默认大偏差速，单位为 count/10ms。 */
#define MOTION_MANAGER_DEFAULT_BIG_DIFF 7

/**
 * @brief Pattern 对应的差速档位和方向。
 * @note  direction 为 1 时左转，为 -1 时右转，为 0 时直行。
 */
typedef struct {
    motion_manager_diff_level_t level;
    int8_t direction;
} motion_manager_pattern_diff_t;

/**
 * @brief Pattern 差速查找表。
 * @note  直线、直角、十字和未知 Pattern 均按直线处理。
 */
static const motion_manager_pattern_diff_t pattern_diff_table[PATTERN_COUNT] = {
    [PATTERN_STRAIGHT] = {MOTION_MANAGER_DIFF_LEVEL_SMALL, 0},
    [PATTERN_SMALL_LEFT] = {MOTION_MANAGER_DIFF_LEVEL_SMALL, 1},
    [PATTERN_SMALL_RIGHT] = {MOTION_MANAGER_DIFF_LEVEL_SMALL, -1},
    [PATTERN_MEDIUM_LEFT] = {MOTION_MANAGER_DIFF_LEVEL_MEDIUM, 1},
    [PATTERN_MEDIUM_RIGHT] = {MOTION_MANAGER_DIFF_LEVEL_MEDIUM, -1},
    [PATTERN_BIG_LEFT] = {MOTION_MANAGER_DIFF_LEVEL_BIG, 1},
    [PATTERN_BIG_RIGHT] = {MOTION_MANAGER_DIFF_LEVEL_BIG, -1},
    [PATTERN_RIGHT_ANGLE_LEFT] = {MOTION_MANAGER_DIFF_LEVEL_SMALL, 0},
    [PATTERN_RIGHT_ANGLE_RIGHT] = {MOTION_MANAGER_DIFF_LEVEL_SMALL, 0},
    [PATTERN_CROSS] = {MOTION_MANAGER_DIFF_LEVEL_SMALL, 0},
    [PATTERN_UNKNOWN] = {MOTION_MANAGER_DIFF_LEVEL_SMALL, 0},
};

/** @brief 运动管理任务节拍标志。 */
volatile uint8_t motion_manager_tick_flag;

/** @brief 当前配置的基础速度。 */
static int16_t configured_base_speed;

/** @brief 三档差速幅值配置。 */
static int16_t configured_diff[MOTION_MANAGER_DIFF_LEVEL_COUNT];

/** @brief 非零表示循迹运动正在运行。 */
static uint8_t is_running;

/**
 * @brief  将基础速度限制到运动控制允许的范围。
 * @param  value  待限制的基础速度。
 * @return 限制到 0～MOTION_CONTROL_TARGET_MAX 后的结果。
 */
static int16_t motion_manager_clamp_base_speed(int16_t value)
{
    if (value < 0) {
        return 0;
    }
    if (value > MOTION_CONTROL_TARGET_MAX) {
        return MOTION_CONTROL_TARGET_MAX;
    }
    return value;
}

/**
 * @brief  根据 Pattern 查表获得带方向的差速。
 * @param  pattern  当前循迹 Pattern。
 * @return 正值表示左转，负值表示右转，0 表示直行。
 */
static int16_t motion_manager_lookup_diff(pattern_state_t pattern)
{
    const motion_manager_pattern_diff_t *entry;
    int16_t diff;

    if ((pattern < PATTERN_STRAIGHT) || (pattern >= PATTERN_COUNT)) {
        return 0;
    }

    entry = &pattern_diff_table[pattern];
    if ((entry->direction == 0)
        || (entry->level >= MOTION_MANAGER_DIFF_LEVEL_COUNT)) {
        return 0;
    }

    diff = configured_diff[entry->level];
    if (diff > configured_base_speed) {
        diff = configured_base_speed;
    }

    return (int16_t)(diff * entry->direction);
}

/**
 * @brief  清除左右轮速度 PID 的历史状态。
 * @return 无。
 */
static void motion_manager_clear_speed_pid(void)
{
    pid_clear(motion_control_pid_speed_left());
    pid_clear(motion_control_pid_speed_right());
}

/**
 * @brief  初始化循迹运动管理模块及默认参数。
 * @return 无。
 */
void motion_manager_init(void)
{
    configured_base_speed = MOTION_MANAGER_DEFAULT_BASE_SPEED;
    configured_diff[MOTION_MANAGER_DIFF_LEVEL_SMALL] = MOTION_MANAGER_DEFAULT_SMALL_DIFF;
    configured_diff[MOTION_MANAGER_DIFF_LEVEL_MEDIUM] = MOTION_MANAGER_DEFAULT_MEDIUM_DIFF;
    configured_diff[MOTION_MANAGER_DIFF_LEVEL_BIG] = MOTION_MANAGER_DEFAULT_BIG_DIFF;
    is_running = 0U;
    motion_manager_tick_flag = 0U;

    motion_control_enable_angle(0U);
    motion_control_set_base_speed(0);
    motion_control_set_diff(0);
    motion_manager_clear_speed_pid();
}

/**
 * @brief  启动循迹运动，使用当前基础速度和三档差速配置。
 * @return 无。
 */
void motion_manager_start(void)
{
    pattern_state_t pattern;

    if (!is_running) {
        motion_manager_clear_speed_pid();
    }

    pattern = pattern_lookup(sensor_read_data());
    motion_control_enable_angle(0U);
    motion_control_set_base_speed(configured_base_speed);
    motion_control_set_diff(motion_manager_lookup_diff(pattern));
    is_running = 1U;
}

/**
 * @brief  停止循迹运动并清除速度 PID 状态。
 * @return 无。
 */
void motion_manager_stop(void)
{
    is_running = 0U;
    motion_control_enable_angle(0U);
    motion_control_set_base_speed(0);
    motion_control_set_diff(0);
    motion_manager_clear_speed_pid();
}

/**
 * @brief  设置循迹基础速度。
 * @param  base_speed  基础速度，单位为 count/10ms，有效范围为 0～100。
 * @return 无。
 */
void motion_manager_set_base_speed(int16_t base_speed)
{
    configured_base_speed = motion_manager_clamp_base_speed(base_speed);

    if (is_running) {
        motion_control_set_base_speed(configured_base_speed);
    }
}

/**
 * @brief  获取当前循迹基础速度配置。
 * @return 基础速度，单位为 count/10ms。
 */
int16_t motion_manager_get_base_speed(void)
{
    return configured_base_speed;
}

/**
 * @brief  设置指定档位的差速幅值。
 * @param  level  差速档位。
 * @param  diff   非负差速幅值，单位为 count/10ms。
 * @note   实际输出不会超过当前基础速度，避免循迹时单侧车轮反转。
 * @return 无。
 */
void motion_manager_set_diff(motion_manager_diff_level_t level, int16_t diff)
{
    if ((level < MOTION_MANAGER_DIFF_LEVEL_SMALL)
        || (level >= MOTION_MANAGER_DIFF_LEVEL_COUNT)) {
        return;
    }

    configured_diff[level] = (diff < 0) ? 0 : diff;
}

/**
 * @brief  获取指定档位的差速幅值。
 * @param  level  差速档位。
 * @return 差速幅值；档位非法时返回 0。
 */
int16_t motion_manager_get_diff(motion_manager_diff_level_t level)
{
    if ((level < MOTION_MANAGER_DIFF_LEVEL_SMALL)
        || (level >= MOTION_MANAGER_DIFF_LEVEL_COUNT)) {
        return 0;
    }

    return configured_diff[level];
}

/**
 * @brief  运行一次循迹管理任务。
 * @note   仅消费最近一次已经完成的传感器数据，不等待 I2C DMA。
 * @return 无。
 */
void motion_manager_task(void)
{
    pattern_state_t pattern;

    if (!motion_manager_tick_flag) {
        return;
    }
    motion_manager_tick_flag = 0U;

    if (!is_running) {
        return;
    }

    pattern = pattern_lookup(sensor_read_data());
    motion_control_set_diff(motion_manager_lookup_diff(pattern));
}
