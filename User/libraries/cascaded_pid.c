/**
 * @file cascaded_pid.c
 * @brief 双轮移动平台串级 PID 逻辑实现。
 * @note 本模块只处理目标、反馈和计算结果，不访问 BSP、Driver 或 HAL。
 * @note 航向外环输出差速目标，左右轮速度环输出独立执行器指令。
 */
#include "cascaded_pid.h"
#include <math.h>

/**
 * @brief  将输入值限制在对称区间内
 * @param  value 待限制的输入值
 * @param  limit 非负绝对值上限
 * @return 限制在 [-limit, limit] 内的结果
 */
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

/**
 * @brief  将任意有限角度归一化到 [-180, 180]
 * @param  angle_deg 待归一化角度，单位：度
 * @return 归一化后的角度，单位：度
 */
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

/**
 * @brief  检查一组 PID 参数是否满足数值约束
 * @param  param 待检查的 PID 参数
 * @retval true 参数有效
 * @retval false 参数为空、包含非有限值或限幅值非法
 */
static bool cascaded_pid_param_is_valid(const pid_param_t *param)
{
    if (!param) {
        return false;
    }
    return isfinite(param->kp) && isfinite(param->ki) && isfinite(param->kd) &&
           isfinite(param->out_max) && isfinite(param->integral_max) && param->out_max > 0.0f &&
           param->integral_max >= 0.0f;
}

/**
 * @brief  检查串级 PID 配置是否满足数值约束
 * @param  config 待检查的串级 PID 配置
 * @retval true 配置有效
 * @retval false 配置为空、PID 参数无效或目标限幅非法
 */
static bool cascaded_pid_config_is_valid(const cascaded_pid_config_t *config)
{
    if (!config) {
        return false;
    }
    return cascaded_pid_param_is_valid(&config->heading_pid) &&
           cascaded_pid_param_is_valid(&config->left_speed_pid) &&
           cascaded_pid_param_is_valid(&config->right_speed_pid) &&
           isfinite(config->speed_target_limit) && isfinite(config->differential_limit) &&
           config->speed_target_limit > 0.0f && config->differential_limit > 0.0f;
}

/**
 * @brief  检查一次串级 PID 输入是否满足数值约束
 * @param  input 待检查的目标和反馈
 * @retval true 输入有效
 * @retval false 输入为空、包含非有限值或航向超出范围
 */
static bool cascaded_pid_input_is_valid(const cascaded_pid_input_t *input)
{
    if (!input) {
        return false;
    }
    return isfinite(input->base_speed) && isfinite(input->target_angle_deg) &&
           isfinite(input->actual_angle_deg) && isfinite(input->left_speed) &&
           isfinite(input->right_speed) && isfinite(input->external_differential) &&
           input->target_angle_deg >= -180.0f && input->target_angle_deg <= 180.0f &&
           input->actual_angle_deg >= -180.0f && input->actual_angle_deg <= 180.0f;
}

/**
 * @brief  将周期角度反馈展开为连续航向
 * @param  controller 已初始化的串级 PID 实例
 * @param  actual_angle_deg 当前周期角度，单位：度
 * @return 展开后的连续角度，单位：度
 */
static float cascaded_pid_update_angle(cascaded_pid_t *controller, float actual_angle_deg)
{
    float delta_deg;

    if (!controller->is_angle_initialized) {
        controller->is_angle_initialized = true;
        controller->actual_angle_last_deg = actual_angle_deg;
        controller->actual_angle_unwrapped = actual_angle_deg;
        return actual_angle_deg;
    }

    delta_deg = cascaded_pid_wrap_180(actual_angle_deg - controller->actual_angle_last_deg);
    controller->actual_angle_unwrapped += delta_deg;
    controller->actual_angle_last_deg = actual_angle_deg;
    return controller->actual_angle_unwrapped;
}

/**
 * @brief  将周期角度目标映射到与连续反馈最近的等价角度
 * @param  target_angle_deg 周期角度目标，单位：度
 * @param  actual_angle_deg 当前周期角度，单位：度
 * @param  actual_angle_unwrapped 当前连续角度，单位：度
 * @return 与连续反馈处于同一展开区间的目标角度，单位：度
 */
static float cascaded_pid_align_target(float target_angle_deg, float actual_angle_deg,
    float actual_angle_unwrapped)
{
    return actual_angle_unwrapped + cascaded_pid_wrap_180(target_angle_deg - actual_angle_deg);
}

/**
 * @brief  初始化串级 PID 实例
 * @param  controller 串级 PID 实例
 * @param  config 初始参数
 * @retval CASCADED_PID_STATUS_OK 初始化成功
 * @retval CASCADED_PID_STATUS_INVALID_ARGUMENT 任一参数为空
 * @retval CASCADED_PID_STATUS_INVALID_CONFIG 配置不满足数值约束
 */
cascaded_pid_status_t cascaded_pid_init(cascaded_pid_t *controller,
    const cascaded_pid_config_t *config)
{
    if (!controller || !config) {
        return CASCADED_PID_STATUS_INVALID_ARGUMENT;
    }
    if (!cascaded_pid_config_is_valid(config)) {
        return CASCADED_PID_STATUS_INVALID_CONFIG;
    }

    controller->is_initialized = false;
    controller->config = *config;
    pid_init(&controller->heading_pid, config->heading_pid.kp, config->heading_pid.ki,
        config->heading_pid.kd, config->heading_pid.out_max, config->heading_pid.integral_max);
    pid_init(&controller->left_speed_pid, config->left_speed_pid.kp, config->left_speed_pid.ki,
        config->left_speed_pid.kd, config->left_speed_pid.out_max,
        config->left_speed_pid.integral_max);
    pid_init(&controller->right_speed_pid, config->right_speed_pid.kp, config->right_speed_pid.ki,
        config->right_speed_pid.kd, config->right_speed_pid.out_max,
        config->right_speed_pid.integral_max);
    controller->actual_angle_last_deg = 0.0f;
    controller->actual_angle_unwrapped = 0.0f;
    controller->is_angle_initialized = false;
    controller->output = (cascaded_pid_output_t){
        0,
    };
    controller->is_initialized = true;
    return CASCADED_PID_STATUS_OK;
}

/**
 * @brief  更新串级 PID 参数并清除动态状态
 * @param  controller 串级 PID 实例
 * @param  config 新参数
 * @retval CASCADED_PID_STATUS_OK 参数已更新且动态状态已清除
 * @retval CASCADED_PID_STATUS_INVALID_ARGUMENT 任一参数为空
 * @retval CASCADED_PID_STATUS_INVALID_CONFIG 配置不满足数值约束
 * @retval CASCADED_PID_STATUS_NOT_INITIALIZED 控制器尚未初始化
 */
cascaded_pid_status_t cascaded_pid_set_config(cascaded_pid_t *controller,
    const cascaded_pid_config_t *config)
{
    cascaded_pid_status_t status;

    if (!controller || !config) {
        return CASCADED_PID_STATUS_INVALID_ARGUMENT;
    }
    if (!controller->is_initialized) {
        return CASCADED_PID_STATUS_NOT_INITIALIZED;
    }
    if (!cascaded_pid_config_is_valid(config)) {
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
 * @brief  清除串级 PID 的积分、微分和航向展开状态
 * @param  controller 串级 PID 实例
 * @retval CASCADED_PID_STATUS_OK 动态状态已清除
 * @retval CASCADED_PID_STATUS_INVALID_ARGUMENT controller 为空
 * @retval CASCADED_PID_STATUS_NOT_INITIALIZED 控制器尚未初始化
 */
cascaded_pid_status_t cascaded_pid_reset(cascaded_pid_t *controller)
{
    if (!controller) {
        return CASCADED_PID_STATUS_INVALID_ARGUMENT;
    }
    if (!controller->is_initialized) {
        return CASCADED_PID_STATUS_NOT_INITIALIZED;
    }

    pid_clear(&controller->heading_pid);
    pid_clear(&controller->left_speed_pid);
    pid_clear(&controller->right_speed_pid);
    controller->actual_angle_last_deg = 0.0f;
    controller->actual_angle_unwrapped = 0.0f;
    controller->is_angle_initialized = false;
    controller->output = (cascaded_pid_output_t){
        0,
    };
    return CASCADED_PID_STATUS_OK;
}

/**
 * @brief  执行一次航向外环和双轮速度内环计算
 * @param  controller 串级 PID 实例
 * @param  input 本次目标和反馈
 * @param  output 接收本次计算结果的存储地址
 * @retval CASCADED_PID_STATUS_OK 计算完成且结果已写入
 * @retval CASCADED_PID_STATUS_INVALID_ARGUMENT 参数为空或输入不满足数值约束
 * @retval CASCADED_PID_STATUS_NOT_INITIALIZED 控制器尚未初始化
 */
cascaded_pid_status_t cascaded_pid_step(cascaded_pid_t *controller,
    const cascaded_pid_input_t *input, cascaded_pid_output_t *output)
{
    float actual_angle_unwrapped;
    float target_angle_aligned;
    float heading_output;
    float differential;
    cascaded_pid_output_t result;

    if (!controller || !input || !output) {
        return CASCADED_PID_STATUS_INVALID_ARGUMENT;
    }
    if (!controller->is_initialized) {
        return CASCADED_PID_STATUS_NOT_INITIALIZED;
    }
    if (!cascaded_pid_input_is_valid(input)) {
        return CASCADED_PID_STATUS_INVALID_ARGUMENT;
    }

    if (input->is_angle_enabled) {
        actual_angle_unwrapped = cascaded_pid_update_angle(controller, input->actual_angle_deg);
        target_angle_aligned = cascaded_pid_align_target(input->target_angle_deg,
            input->actual_angle_deg, actual_angle_unwrapped);
        heading_output =
            pid_calc(&controller->heading_pid, target_angle_aligned, actual_angle_unwrapped);
    } else {
        pid_clear(&controller->heading_pid);
        controller->is_angle_initialized = false;
        heading_output = 0.0f;
    }

    differential = cascaded_pid_clamp(heading_output + input->external_differential,
        controller->config.differential_limit);
    result.left_speed_target =
        cascaded_pid_clamp(input->base_speed - differential, controller->config.speed_target_limit);
    result.right_speed_target =
        cascaded_pid_clamp(input->base_speed + differential, controller->config.speed_target_limit);
    result.heading_output = heading_output;
    result.differential = differential;
    result.left_output =
        pid_calc(&controller->left_speed_pid, result.left_speed_target, input->left_speed);
    result.right_output =
        pid_calc(&controller->right_speed_pid, result.right_speed_target, input->right_speed);

    controller->output = result;
    *output = result;
    return CASCADED_PID_STATUS_OK;
}

/**
 * @brief  读取最近一次串级 PID 输出
 * @param  controller 串级 PID 实例
 * @param  output 接收输出快照的存储地址
 * @retval CASCADED_PID_STATUS_OK 输出快照已写入
 * @retval CASCADED_PID_STATUS_INVALID_ARGUMENT 任一参数为空
 * @retval CASCADED_PID_STATUS_NOT_INITIALIZED 控制器尚未初始化
 */
cascaded_pid_status_t cascaded_pid_get_output(const cascaded_pid_t *controller,
    cascaded_pid_output_t *output)
{
    if (!controller || !output) {
        return CASCADED_PID_STATUS_INVALID_ARGUMENT;
    }
    if (!controller->is_initialized) {
        return CASCADED_PID_STATUS_NOT_INITIALIZED;
    }

    *output = controller->output;
    return CASCADED_PID_STATUS_OK;
}
