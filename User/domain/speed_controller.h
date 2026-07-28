#ifndef AUTO_BALL_CAR_USER_DOMAIN_SPEED_CONTROLLER_H
#define AUTO_BALL_CAR_USER_DOMAIN_SPEED_CONTROLLER_H /* 头文件保护 */

#include "cascaded_pid.h"
#include "status.h"
#include <stdbool.h>
#include <stdint.h>

/* 双轮速度闭环一次计算的输入 */
typedef struct {
    int32_t left_delta; /* 左轮 10 ms 编码器增量 */
    int32_t right_delta; /* 右轮 10 ms 编码器增量 */
} speed_controller_feedback_t;

/* 双轮速度闭环最近一次计算结果 */
typedef struct {
    float target; /* 双轮共同目标，单位：count/10ms */
    float left_actual; /* 左轮反馈，单位：count/10ms */
    float right_actual; /* 右轮反馈，单位：count/10ms */
    float left_output; /* 左轮千分比输出，正负 1000 对应满占空比 */
    float right_output; /* 右轮千分比输出，正负 1000 对应满占空比 */
} speed_controller_output_t;

/* 仅启用串级控制器速度内环的领域对象 */
typedef struct {
    cascaded_pid_t cascaded_pid; /* 串级 PID 算法实例 */
    speed_controller_output_t output; /* 最近一次输出快照 */
    float target; /* 当前双轮速度目标，单位：count/10ms */
    bool is_active; /* 速度闭环运行标志 */
    bool is_initialized; /* 领域对象初始化完成标志 */
} speed_controller_t;

/* 生命周期、运行控制和计算接口 */
status_code_t speed_controller_init_default(speed_controller_t *controller);
status_code_t speed_controller_init(speed_controller_t *controller,
    const cascaded_pid_config_t *config);
status_code_t speed_controller_start(speed_controller_t *controller, float target);
status_code_t speed_controller_stop(speed_controller_t *controller);
status_code_t speed_controller_get_common_speed_pid(const speed_controller_t *controller,
    pid_param_t *parameters);
status_code_t speed_controller_apply_common_speed_pid(speed_controller_t *controller,
    const pid_param_t *parameters);
status_code_t speed_controller_step(speed_controller_t *controller,
    const speed_controller_feedback_t *feedback, speed_controller_output_t *output);
status_code_t speed_controller_get_output(const speed_controller_t *controller,
    speed_controller_output_t *output);

#endif /* AUTO_BALL_CAR_USER_DOMAIN_SPEED_CONTROLLER_H */
