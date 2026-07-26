/**
 * @file cascaded_pid.c
 * @brief 双轮移动平台串级 PID 逻辑实现。
 * @note 本模块只处理目标、反馈和计算结果，不访问 BSP、Driver 或 HAL。
 * @note 航向外环输出差速目标，左右轮速度环输出独立执行器指令。
 */
#include "cascaded_pid.h"

#include <math.h>
#include <stddef.h>

static float cascaded_pid_clamp(float value, float limit)
{
    if (value > limit) {
        return limit;
    }
    if (value < -limit) {
        return -limit;
    }
    return value;
}

static float cascaded_pid_wrap_180(float angle_deg)
{
    while (angle_deg > 180.0f) {
        angle_deg -= 360.0f;
    }
    while (angle_deg < -180.0f) {
        angle_deg += 360.0f;
    }
    return angle_deg;
}

static uint8_t cascaded_pid_param_is_valid(const pid_param_t *param)
{
    if (param == NULL) {
        return 0U;
    }
    return (uint8_t)(isfinite(param->kp) && isfinite(param->ki) &&
                     isfinite(param->kd) && isfinite(param->out_max) &&
                     isfinite(param->integral_max) && param->out_max > 0.0f &&
                     param->integral_max >= 0.0f);
}

static uint8_t cascaded_pid_config_is_valid(const cascaded_pid_config_t *config)
{
    if (config == NULL) {
        return 0U;
    }
    return (uint8_t)(cascaded_pid_param_is_valid(&config->heading_pid) &&
                     cascaded_pid_param_is_valid(&config->left_speed_pid) &&
                     cascaded_pid_param_is_valid(&config->right_speed_pid) &&
                     isfinite(config->speed_target_limit) &&
                     isfinite(config->differential_limit) &&
                     config->speed_target_limit > 0.0f &&
                     config->differential_limit > 0.0f);
}

static uint8_t cascaded_pid_input_is_valid(const cascaded_pid_input_t *input)
{
    if (input == NULL) {
        return 0U;
    }
    return (uint8_t)(isfinite(input->base_speed) &&
                     isfinite(input->target_angle_deg) &&
                     isfinite(input->actual_angle_deg) &&
                     isfinite(input->left_speed) &&
                     isfinite(input->right_speed) &&
                     isfinite(input->external_differential) &&
                     input->target_angle_deg >= -180.0f &&
                     input->target_angle_deg <= 180.0f &&
                     input->actual_angle_deg >= -180.0f &&
                     input->actual_angle_deg <= 180.0f);
}

static float cascaded_pid_update_angle(cascaded_pid_t *controller, float actual_angle_deg)
{
    float delta_deg;

    if (controller->is_angle_initialized == 0U) {
        controller->is_angle_initialized = 1U;
        controller->actual_angle_last_deg = actual_angle_deg;
        controller->actual_angle_unwrapped = actual_angle_deg;
        return actual_angle_deg;
    }

    delta_deg = cascaded_pid_wrap_180(actual_angle_deg -
                                      controller->actual_angle_last_deg);
    controller->actual_angle_unwrapped += delta_deg;
    controller->actual_angle_last_deg = actual_angle_deg;
    return controller->actual_angle_unwrapped;
}

static float cascaded_pid_align_target(
    float target_angle_deg,
    float actual_angle_deg,
    float actual_angle_unwrapped)
{
    return actual_angle_unwrapped +
           cascaded_pid_wrap_180(target_angle_deg - actual_angle_deg);
}

/**
 * @brief 初始化串级 PID 实例。
 * @param controller 串级 PID 实例。
 * @param config 初始参数。
 * @return 初始化状态。
 */
cascaded_pid_status_t cascaded_pid_init(
    cascaded_pid_t *controller,
    const cascaded_pid_config_t *config)
{
    if ((controller == NULL) || (config == NULL)) {
        return CASCADED_PID_STATUS_INVALID_ARGUMENT;
    }
    if (cascaded_pid_config_is_valid(config) == 0U) {
        return CASCADED_PID_STATUS_INVALID_CONFIG;
    }

    controller->is_initialized = 0U;
    controller->config = *config;
    pid_init(&controller->heading_pid, config->heading_pid.kp,
             config->heading_pid.ki, config->heading_pid.kd,
             config->heading_pid.out_max, config->heading_pid.integral_max);
    pid_init(&controller->left_speed_pid, config->left_speed_pid.kp,
             config->left_speed_pid.ki, config->left_speed_pid.kd,
             config->left_speed_pid.out_max, config->left_speed_pid.integral_max);
    pid_init(&controller->right_speed_pid, config->right_speed_pid.kp,
             config->right_speed_pid.ki, config->right_speed_pid.kd,
             config->right_speed_pid.out_max, config->right_speed_pid.integral_max);
    controller->actual_angle_last_deg = 0.0f;
    controller->actual_angle_unwrapped = 0.0f;
    controller->is_angle_initialized = 0U;
    controller->output = (cascaded_pid_output_t){0};
    controller->is_initialized = 1U;
    return CASCADED_PID_STATUS_OK;
}

/**
 * @brief 更新串级 PID 参数并清除动态状态。
 * @param controller 串级 PID 实例。
 * @param config 新参数。
 * @return 更新状态。
 */
cascaded_pid_status_t cascaded_pid_set_config(
    cascaded_pid_t *controller,
    const cascaded_pid_config_t *config)
{
    cascaded_pid_status_t status;

    if ((controller == NULL) || (config == NULL)) {
        return CASCADED_PID_STATUS_INVALID_ARGUMENT;
    }
    if (controller->is_initialized == 0U) {
        return CASCADED_PID_STATUS_NOT_INITIALIZED;
    }
    if (cascaded_pid_config_is_valid(config) == 0U) {
        return CASCADED_PID_STATUS_INVALID_CONFIG;
    }

    controller->config = *config;
    pid_set_param(&controller->heading_pid, &config->heading_pid);
    pid_set_param(&controller->left_speed_pid, &config->left_speed_pid);
    pid_set_param(&controller->right_speed_pid, &config->right_speed_pid);
    status = cascaded_pid_reset(controller);
    return status;
}

/**
 * @brief 清除串级 PID 的积分、微分和航向展开状态。
 * @param controller 串级 PID 实例。
 * @return 清除状态。
 */
cascaded_pid_status_t cascaded_pid_reset(cascaded_pid_t *controller)
{
    if (controller == NULL) {
        return CASCADED_PID_STATUS_INVALID_ARGUMENT;
    }
    if (controller->is_initialized == 0U) {
        return CASCADED_PID_STATUS_NOT_INITIALIZED;
    }

    pid_clear(&controller->heading_pid);
    pid_clear(&controller->left_speed_pid);
    pid_clear(&controller->right_speed_pid);
    controller->actual_angle_last_deg = 0.0f;
    controller->actual_angle_unwrapped = 0.0f;
    controller->is_angle_initialized = 0U;
    controller->output = (cascaded_pid_output_t){0};
    return CASCADED_PID_STATUS_OK;
}

/**
 * @brief 执行一次航向外环和双轮速度内环计算。
 * @param controller 串级 PID 实例。
 * @param input 本次目标和反馈。
 * @param output 输出快照。
 * @return 计算状态。
 */
cascaded_pid_status_t cascaded_pid_step(
    cascaded_pid_t *controller,
    const cascaded_pid_input_t *input,
    cascaded_pid_output_t *output)
{
    float actual_angle_unwrapped;
    float target_angle_aligned;
    float heading_output;
    float differential;
    cascaded_pid_output_t result;

    if ((controller == NULL) || (input == NULL) || (output == NULL)) {
        return CASCADED_PID_STATUS_INVALID_ARGUMENT;
    }
    if (controller->is_initialized == 0U) {
        return CASCADED_PID_STATUS_NOT_INITIALIZED;
    }
    if (cascaded_pid_input_is_valid(input) == 0U) {
        return CASCADED_PID_STATUS_INVALID_ARGUMENT;
    }

    if (input->is_angle_enabled != 0U) {
        actual_angle_unwrapped = cascaded_pid_update_angle(
            controller, input->actual_angle_deg);
        target_angle_aligned = cascaded_pid_align_target(
            input->target_angle_deg, input->actual_angle_deg,
            actual_angle_unwrapped);
        heading_output = pid_calc(&controller->heading_pid,
                                  target_angle_aligned, actual_angle_unwrapped);
    } else {
        pid_clear(&controller->heading_pid);
        controller->is_angle_initialized = 0U;
        heading_output = 0.0f;
    }

    differential = cascaded_pid_clamp(
        heading_output + input->external_differential,
        controller->config.differential_limit);
    result.left_speed_target = cascaded_pid_clamp(
        input->base_speed - differential,
        controller->config.speed_target_limit);
    result.right_speed_target = cascaded_pid_clamp(
        input->base_speed + differential,
        controller->config.speed_target_limit);
    result.heading_output = heading_output;
    result.differential = differential;
    result.left_output = pid_calc(&controller->left_speed_pid,
                                  result.left_speed_target, input->left_speed);
    result.right_output = pid_calc(&controller->right_speed_pid,
                                   result.right_speed_target, input->right_speed);

    controller->output = result;
    *output = result;
    return CASCADED_PID_STATUS_OK;
}

/**
 * @brief 读取最近一次串级 PID 输出。
 * @param controller 串级 PID 实例。
 * @param output 输出快照。
 * @return 查询状态。
 */
cascaded_pid_status_t cascaded_pid_get_output(
    const cascaded_pid_t *controller,
    cascaded_pid_output_t *output)
{
    if ((controller == NULL) || (output == NULL)) {
        return CASCADED_PID_STATUS_INVALID_ARGUMENT;
    }
    if (controller->is_initialized == 0U) {
        return CASCADED_PID_STATUS_NOT_INITIALIZED;
    }

    *output = controller->output;
    return CASCADED_PID_STATUS_OK;
}
