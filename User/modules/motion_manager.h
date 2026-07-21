#ifndef MOTION_MANAGER_H
#define MOTION_MANAGER_H

#include <stdint.h>

/* 循迹差速档位。 */
typedef enum {
    MOTION_MANAGER_DIFF_LEVEL_SMALL = 0, /* 微偏差速。 */
    MOTION_MANAGER_DIFF_LEVEL_MEDIUM,    /* 中偏差速。 */
    MOTION_MANAGER_DIFF_LEVEL_BIG,       /* 大偏差速。 */
    MOTION_MANAGER_DIFF_LEVEL_COUNT,     /* 差速档位数量。 */
} motion_manager_diff_level_t;

/* TIM6 中断置位、主循环消费的运动管理任务标志。 */
extern volatile uint8_t motion_manager_tick_flag;

/* 生命周期与非阻塞任务接口。 */
void motion_manager_init(void);
void motion_manager_task(void);

/* 上层运动控制接口。 */
void motion_manager_start(void);
void motion_manager_stop(void);

/* 基础速度配置，单位为 count/10ms。 */
void motion_manager_set_base_speed(int16_t base_speed);
int16_t motion_manager_get_base_speed(void);

/* 三档差速幅值配置，单位为 count/10ms。 */
void motion_manager_set_diff(motion_manager_diff_level_t level, int16_t diff);
int16_t motion_manager_get_diff(motion_manager_diff_level_t level);

#endif /* MOTION_MANAGER_H */
