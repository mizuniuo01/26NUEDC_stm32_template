#ifndef AUTO_BALL_CAR_USER_DOMAIN_MOTION_PLANNER_H
#define AUTO_BALL_CAR_USER_DOMAIN_MOTION_PLANNER_H /* 头文件保护 */

#include "status.h"
#include <stdbool.h>
#include <stdint.h>

/* 运动规划器当前执行的任务状态 */
typedef enum {
    MOTION_PLANNER_STATE_IDLE = 0,   /* 等待新规划任务 */
    MOTION_PLANNER_STATE_MOVE = 1,   /* 执行直线移动任务 */
    MOTION_PLANNER_STATE_ROTATE = 2, /* 执行原地旋转任务 */
} motion_planner_state_t;

/* 规划器配置，单位和轮距换算由板级组合根提供。 */
typedef struct {
    float encoder_counts_per_mm;   /* 编码器计数到毫米的换算系数 */
    float rotate_dead_zone_deg;    /* 旋转完成判定死区，单位：度 */
} motion_planner_config_t;

/* 普通闭环命令，规划完成后恢复到该命令。 */
typedef struct {
    float base_speed;              /* 基础速度目标 */
    float target_angle_deg;        /* 航向目标，单位：度，范围 [-180, 180] */
    float external_differential;   /* 外部差速补偿 */
    bool is_angle_enabled;         /* 航向环启用标志 */
} motion_planner_normal_command_t;

/* 规划器向串级 PID 或其他执行服务提交的运动命令。 */
typedef struct {
    float base_speed;              /* 基础速度目标 */
    float target_angle_deg;        /* 航向目标，单位：度，范围 [-180, 180] */
    float external_differential;   /* 外部差速补偿 */
    bool is_angle_enabled;         /* 航向环启用标志 */
} motion_planner_command_t;

/* 每个规划周期由 BSP/驱动适配层提交的反馈快照。 */
typedef struct {
    float left_encoder_delta;      /* 本周期左轮增量计数 */
    float right_encoder_delta;     /* 本周期右轮增量计数 */
    float actual_angle_deg;        /* 当前航向，单位：度，范围 [-180, 180] */
} motion_planner_feedback_t;

/* 规划器实例，由调用方分配和持有。 */
typedef struct {
    motion_planner_config_t config; /* 配置快照 */
    motion_planner_normal_command_t normal_command; /* 普通闭环命令 */
    motion_planner_command_t command; /* 当前命令 */
    motion_planner_state_t state;   /* 当前规划状态 */
    float target_distance_counts;   /* 当前移动目标计数 */
    float accumulated_distance_counts; /* 已累计计数 */
    float rotate_target_angle_deg;  /* 当前旋转目标 */
    float rotate_speed;             /* 当前旋转基础速度 */
    float move_speed;               /* 当前移动基础速度绝对值 */
    int8_t move_direction;          /* 移动方向，取值为 -1 或 1 */
    bool is_first_move_sample;      /* 是否跳过首次距离采样 */
    bool is_halted;                 /* 是否需要在下一周期保持安全停止 */
    bool is_initialized;            /* 实例是否已初始化 */
} motion_planner_t;

/* 生命周期与配置接口 */
status_code_t motion_planner_init(motion_planner_t *planner, const motion_planner_config_t *config,
    const motion_planner_normal_command_t *normal_command);
status_code_t motion_planner_set_normal_command(motion_planner_t *planner,
    const motion_planner_normal_command_t *command);

/* 规划任务控制接口 */
status_code_t motion_planner_start_move(motion_planner_t *planner, float distance_mm, float speed);
status_code_t motion_planner_start_rotate(motion_planner_t *planner, float delta_angle_deg,
    float speed);
status_code_t motion_planner_step(motion_planner_t *planner,
    const motion_planner_feedback_t *feedback, motion_planner_command_t *command);
status_code_t motion_planner_cancel(motion_planner_t *planner);
status_code_t motion_planner_halt(motion_planner_t *planner);
status_code_t motion_planner_replan_remaining(motion_planner_t *planner, float remaining_mm);

/* 状态查询接口 */
status_code_t motion_planner_get_command(const motion_planner_t *planner,
    motion_planner_command_t *command);
status_code_t motion_planner_get_state(const motion_planner_t *planner,
    motion_planner_state_t *state);
status_code_t motion_planner_get_elapsed_mm(const motion_planner_t *planner, float *elapsed_mm);
status_code_t motion_planner_get_remaining_mm(const motion_planner_t *planner, float *remaining_mm);

#endif /* AUTO_BALL_CAR_USER_DOMAIN_MOTION_PLANNER_H */
