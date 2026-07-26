/**
 * @file cascaded_pid.h
 * @brief 双轮移动平台的串级 PID 逻辑接口。
 */
#ifndef AUTO_BALL_CAR_USER_LIBRARIES_CASCADED_PID_H
#define AUTO_BALL_CAR_USER_LIBRARIES_CASCADED_PID_H

#include <stdint.h>

#include "pid.h"

typedef enum {
    CASCADED_PID_STATUS_OK = 0,
    CASCADED_PID_STATUS_INVALID_ARGUMENT = 1,
    CASCADED_PID_STATUS_INVALID_CONFIG = 2,
    CASCADED_PID_STATUS_NOT_INITIALIZED = 3,
} cascaded_pid_status_t;

/** 串级 PID 配置，外环为航向环，内环为左右轮速度环。 */
typedef struct {
    pid_param_t heading_pid;       /* 航向外环参数，输出为差速目标 */
    pid_param_t left_speed_pid;    /* 左轮速度内环参数 */
    pid_param_t right_speed_pid;   /* 右轮速度内环参数 */
    float speed_target_limit;      /* 单轮速度目标绝对值上限 */
    float differential_limit;      /* 外环和外部差速之和的绝对值上限 */
} cascaded_pid_config_t;

/** 串级 PID 一次计算所需的反馈和目标。 */
typedef struct {
    float base_speed;              /* 车体基础速度目标 */
    float target_angle_deg;        /* 航向目标，单位：度，范围 [-180, 180] */
    float actual_angle_deg;        /* 当前航向，单位：度，范围 [-180, 180] */
    float left_speed;              /* 左轮反馈速度 */
    float right_speed;             /* 右轮反馈速度 */
    float external_differential;   /* 感知或上层提供的差速补偿 */
    uint8_t is_angle_enabled;      /* 非零时运行航向外环 */
} cascaded_pid_input_t;

/** 串级 PID 一次计算输出的左右轮目标和执行器输出。 */
typedef struct {
    float left_speed_target;       /* 左轮速度目标 */
    float right_speed_target;      /* 右轮速度目标 */
    float heading_output;          /* 航向外环输出 */
    float differential;             /* 限幅后的总差速 */
    float left_output;             /* 左轮内环输出 */
    float right_output;            /* 右轮内环输出 */
} cascaded_pid_output_t;

/** 串级 PID 实例，由调用方分配和持有。 */
typedef struct {
    cascaded_pid_config_t config; /* 当前配置快照 */
    pid_t heading_pid;             /* 航向外环 */
    pid_t left_speed_pid;          /* 左轮速度内环 */
    pid_t right_speed_pid;         /* 右轮速度内环 */
    float actual_angle_last_deg;   /* 上一次原始航向 */
    float actual_angle_unwrapped;  /* 展开的连续航向 */
    cascaded_pid_output_t output;  /* 最近一次计算结果 */
    uint8_t is_angle_initialized;  /* 航向展开器是否已经初始化 */
    uint8_t is_initialized;        /* 实例是否已经初始化 */
} cascaded_pid_t;

cascaded_pid_status_t cascaded_pid_init(
    cascaded_pid_t *controller,
    const cascaded_pid_config_t *config);
cascaded_pid_status_t cascaded_pid_set_config(
    cascaded_pid_t *controller,
    const cascaded_pid_config_t *config);
cascaded_pid_status_t cascaded_pid_reset(cascaded_pid_t *controller);
cascaded_pid_status_t cascaded_pid_step(
    cascaded_pid_t *controller,
    const cascaded_pid_input_t *input,
    cascaded_pid_output_t *output);
cascaded_pid_status_t cascaded_pid_get_output(
    const cascaded_pid_t *controller,
    cascaded_pid_output_t *output);

#endif /* AUTO_BALL_CAR_USER_LIBRARIES_CASCADED_PID_H */
