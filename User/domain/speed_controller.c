/**
 * @file speed_controller.c
 * @brief 使用串级 PID 的双轮速度内环实现，不启用航向外环。
 */
#include "speed_controller.h"
#include <math.h>

/**
 * @brief  使用经本车速度环上板验证的参数初始化控制器
 * @param  controller 速度闭环实例
 * @retval STATUS_OK 初始化成功
 * @retval STATUS_INVALID_ARGUMENT controller 为空
 */
status_code_t speed_controller_init_default(speed_controller_t *controller)
{
    const pid_param_t unused_heading_pid = {
        .kp = 1.0F,
        .ki = 0.0F,
        .kd = 0.0F,
        .out_max = 1.0F,
        .integral_max = 0.0F,
    };
    const pid_param_t speed_pid = {
        .kp = 15.0F,
        .ki = 1.0F,
        .kd = 0.0F,
        .out_max = 1000.0F,
        .integral_max = 4000.0F,
    };
    const cascaded_pid_config_t config = {
        .heading_pid = unused_heading_pid,
        .left_speed_pid = speed_pid,
        .right_speed_pid = speed_pid,
        .speed_target_limit = 100.0F,
        .differential_limit = 1.0F,
    };

    return speed_controller_init(controller, &config);
}

/**
 * @brief  初始化双轮速度闭环领域对象
 * @param  controller 速度闭环实例
 * @param  config 串级 PID 配置
 * @retval STATUS_OK 初始化成功
 * @retval STATUS_INVALID_ARGUMENT 任一参数为空或配置无效
 */
status_code_t speed_controller_init(speed_controller_t *controller,
    const cascaded_pid_config_t *config)
{
    if (!controller || !config) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (cascaded_pid_init(&controller->cascaded_pid, config) != CASCADED_PID_STATUS_OK) {
        return STATUS_INVALID_ARGUMENT;
    }
    controller->output = (speed_controller_output_t){0};
    controller->target = 0.0F;
    controller->is_active = false;
    controller->is_initialized = true;
    return STATUS_OK;
}

/**
 * @brief  清除历史状态并启动双轮速度闭环
 * @param  controller 已初始化的速度闭环实例
 * @param  target 双轮共同目标，单位：count/10ms
 * @retval STATUS_OK 速度闭环已启动
 * @retval STATUS_INVALID_ARGUMENT controller 为空或 target 非有限值
 * @retval STATUS_NOT_INITIALIZED 速度闭环尚未初始化
 * @retval STATUS_OUT_OF_RANGE target 超过配置的速度目标上限
 */
status_code_t speed_controller_start(speed_controller_t *controller, float target)
{
    if (!controller || !isfinite(target)) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!controller->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (fabsf(target) > controller->cascaded_pid.config.speed_target_limit) {
        return STATUS_OUT_OF_RANGE;
    }
    if (cascaded_pid_reset(&controller->cascaded_pid) != CASCADED_PID_STATUS_OK) {
        return STATUS_STATE_ERROR;
    }
    controller->output = (speed_controller_output_t){0};
    controller->target = target;
    controller->is_active = true;
    return STATUS_OK;
}

/**
 * @brief  停止双轮速度闭环并清除动态状态
 * @param  controller 速度闭环实例
 * @retval STATUS_OK 速度闭环已停止
 * @retval STATUS_INVALID_ARGUMENT controller 为空
 * @retval STATUS_NOT_INITIALIZED 速度闭环尚未初始化
 */
status_code_t speed_controller_stop(speed_controller_t *controller)
{
    if (!controller) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!controller->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (cascaded_pid_reset(&controller->cascaded_pid) != CASCADED_PID_STATUS_OK) {
        return STATUS_STATE_ERROR;
    }
    controller->output = (speed_controller_output_t){0};
    controller->target = 0.0F;
    controller->is_active = false;
    return STATUS_OK;
}

/**
 * @brief  读取左右速度环共用的 PID 参数
 * @param  controller 已初始化的速度闭环实例
 * @param  parameters 接收共同 PID 参数的存储地址
 * @retval STATUS_OK 参数已写入
 * @retval STATUS_INVALID_ARGUMENT 任一参数为空
 * @retval STATUS_NOT_INITIALIZED 速度闭环尚未初始化
 * @retval STATUS_STATE_ERROR 左右速度环当前参数不一致
 */
status_code_t speed_controller_get_common_speed_pid(const speed_controller_t *controller,
    pid_param_t *parameters)
{
    const pid_param_t *left;
    const pid_param_t *right;

    if (!controller || !parameters) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!controller->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    left = &controller->cascaded_pid.config.left_speed_pid;
    right = &controller->cascaded_pid.config.right_speed_pid;
    if ((left->kp != right->kp) || (left->ki != right->ki) || (left->kd != right->kd) ||
        (left->out_max != right->out_max) ||
        (left->integral_max != right->integral_max)) {
        return STATUS_STATE_ERROR;
    }
    *parameters = *left;
    return STATUS_OK;
}

/**
 * @brief  原子更新左右速度环的共同 PID 参数并清除历史状态
 * @param  controller 已初始化的速度闭环实例
 * @param  parameters 待同步到左右速度环的 PID 参数
 * @retval STATUS_OK 左右速度环已同步且动态历史已清除
 * @retval STATUS_INVALID_ARGUMENT 参数为空或 PID 数值不满足领域约束
 * @retval STATUS_NOT_INITIALIZED 速度闭环尚未初始化
 * @retval STATUS_STATE_ERROR 串级 PID 拒绝了整体配置
 */
status_code_t speed_controller_apply_common_speed_pid(speed_controller_t *controller,
    const pid_param_t *parameters)
{
    cascaded_pid_config_t config;
    cascaded_pid_status_t status;

    if (!controller || !parameters) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!controller->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    config = controller->cascaded_pid.config;
    config.left_speed_pid = *parameters;
    config.right_speed_pid = *parameters;
    status = cascaded_pid_set_config(&controller->cascaded_pid, &config);
    if (status == CASCADED_PID_STATUS_INVALID_CONFIG) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (status != CASCADED_PID_STATUS_OK) {
        return STATUS_STATE_ERROR;
    }
    controller->output = (speed_controller_output_t){0};
    return STATUS_OK;
}

/**
 * @brief  使用一份新的 10 ms 编码器反馈计算双轮 PWM
 * @param  controller 已启动的速度闭环实例
 * @param  feedback 本周期双编码器反馈
 * @param  output 接收本次控制结果的存储地址
 * @retval STATUS_OK 速度内环计算完成
 * @retval STATUS_INVALID_ARGUMENT 任一参数为空
 * @retval STATUS_NOT_INITIALIZED 速度闭环尚未初始化
 * @retval STATUS_STATE_ERROR 速度闭环未启动或串级 PID 计算失败
 */
status_code_t speed_controller_step(speed_controller_t *controller,
    const speed_controller_feedback_t *feedback, speed_controller_output_t *output)
{
    cascaded_pid_output_t cascaded_output;
    cascaded_pid_input_t input;

    if (!controller || !feedback || !output) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!controller->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (!controller->is_active) {
        return STATUS_STATE_ERROR;
    }
    input = (cascaded_pid_input_t){
        .base_speed = controller->target,
        .target_angle_deg = 0.0F,
        .actual_angle_deg = 0.0F,
        .left_speed = (float)feedback->left_delta,
        .right_speed = (float)feedback->right_delta,
        .external_differential = 0.0F,
        .is_angle_enabled = false,
    };
    if (cascaded_pid_step(&controller->cascaded_pid, &input, &cascaded_output) !=
        CASCADED_PID_STATUS_OK) {
        return STATUS_STATE_ERROR;
    }
    controller->output = (speed_controller_output_t){
        .target = controller->target,
        .left_actual = input.left_speed,
        .right_actual = input.right_speed,
        .left_output = cascaded_output.left_output,
        .right_output = cascaded_output.right_output,
    };
    *output = controller->output;
    return STATUS_OK;
}

/**
 * @brief  获取双轮速度闭环最近一次计算结果
 * @param  controller 速度闭环实例
 * @param  output 接收结果快照的存储地址
 * @retval STATUS_OK 结果快照已写入
 * @retval STATUS_INVALID_ARGUMENT 任一参数为空
 * @retval STATUS_NOT_INITIALIZED 速度闭环尚未初始化
 */
status_code_t speed_controller_get_output(const speed_controller_t *controller,
    speed_controller_output_t *output)
{
    if (!controller || !output) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!controller->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    *output = controller->output;
    return STATUS_OK;
}
