/**
 * @file motion_planner.c
 * @brief 基于距离和航向的非阻塞运动规划领域实现。
 * @note 本模块不读取编码器/陀螺仪，也不写入电机；反馈和命令通过值对象交换。
 * @note 保留原工程的 MOVE/ROTATE/NORMAL 语义、首次采样跳过和角度死区。
 */
#include "motion_planner.h"
#include <math.h>

/**
 * @brief  将任意有限角度归一化到 [-180, 180]
 * @param  angle_deg 待归一化角度，单位：度
 * @return 归一化后的角度，单位：度
 */
static float motion_planner_wrap_180(float angle_deg)
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
 * @brief  计算单精度浮点数的绝对值
 * @param  value 输入值
 * @return value 的非负绝对值
 */
static float motion_planner_abs(float value)
{
    return value < 0.0f ? -value : value;
}

/**
 * @brief  检查运动规划器配置是否满足数值约束
 * @param  config 待检查的配置
 * @retval true 配置有效
 * @retval false 配置为空、包含非有限值或超出约束
 */
static bool motion_planner_config_is_valid(const motion_planner_config_t *config)
{
    return config && isfinite(config->encoder_counts_per_mm) &&
           isfinite(config->rotate_dead_zone_deg) && config->encoder_counts_per_mm > 0.0f &&
           config->rotate_dead_zone_deg >= 0.0f;
}

/**
 * @brief  检查普通闭环命令是否满足数值约束
 * @param  command 待检查的普通闭环命令
 * @retval true 命令有效
 * @retval false 命令为空、包含非有限值或航向超出范围
 */
static bool
motion_planner_normal_command_is_valid(const motion_planner_normal_command_t *command)
{
    return command && isfinite(command->base_speed) && isfinite(command->target_angle_deg) &&
           isfinite(command->external_differential) && command->target_angle_deg >= -180.0f &&
           command->target_angle_deg <= 180.0f;
}

/**
 * @brief  将普通闭环命令转换为规划器输出命令
 * @param  normal_command 有效的普通闭环命令
 * @return 字段值相同的规划器输出命令
 */
static motion_planner_command_t
motion_planner_command_from_normal(const motion_planner_normal_command_t *normal_command)
{
    motion_planner_command_t command = {
        .base_speed = normal_command->base_speed,
        .target_angle_deg = normal_command->target_angle_deg,
        .external_differential = normal_command->external_differential,
        .is_angle_enabled = normal_command->is_angle_enabled,
    };

    return command;
}

/**
 * @brief  将规划器输出更新为零速度安全停止命令
 * @param  planner 已初始化的规划器实例
 */
static void motion_planner_set_stop_command(motion_planner_t *planner)
{
    planner->command.base_speed = 0.0f;
    planner->command.external_differential = 0.0f;
    planner->command.is_angle_enabled = planner->normal_command.is_angle_enabled;
}

/**
 * @brief  根据当前移动规划更新输出命令
 * @param  planner 已初始化且处于移动任务的规划器实例
 */
static void motion_planner_set_move_command(motion_planner_t *planner)
{
    planner->command = motion_planner_command_from_normal(&planner->normal_command);
    planner->command.base_speed = (float)planner->move_direction * planner->move_speed;
    planner->command.external_differential = 0.0f;
    planner->command.is_angle_enabled = true;
}

/**
 * @brief  根据当前旋转规划更新输出命令
 * @param  planner 已初始化且处于旋转任务的规划器实例
 */
static void motion_planner_set_rotate_command(motion_planner_t *planner)
{
    planner->command = motion_planner_command_from_normal(&planner->normal_command);
    planner->command.base_speed = planner->rotate_speed;
    planner->command.target_angle_deg = planner->rotate_target_angle_deg;
    planner->command.external_differential = 0.0f;
    planner->command.is_angle_enabled = true;
}

/**
 * @brief  初始化运动规划器
 * @param  planner 规划器实例
 * @param  config 换算和完成判定配置
 * @param  normal_command 默认普通闭环命令
 * @retval STATUS_OK 初始化成功
 * @retval STATUS_INVALID_ARGUMENT 任一参数为空
 * @retval STATUS_OUT_OF_RANGE 配置或普通闭环命令不满足数值约束
 */
status_code_t motion_planner_init(motion_planner_t *planner, const motion_planner_config_t *config,
    const motion_planner_normal_command_t *normal_command)
{
    if (!planner || !config || !normal_command) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!motion_planner_config_is_valid(config) ||
        !motion_planner_normal_command_is_valid(normal_command)) {
        return STATUS_OUT_OF_RANGE;
    }

    planner->is_initialized = false;
    planner->config = *config;
    planner->normal_command = *normal_command;
    planner->command = motion_planner_command_from_normal(normal_command);
    planner->state = MOTION_PLANNER_STATE_IDLE;
    planner->target_distance_counts = 0.0f;
    planner->accumulated_distance_counts = 0.0f;
    planner->rotate_target_angle_deg = normal_command->target_angle_deg;
    planner->rotate_speed = 0.0f;
    planner->move_speed = 0.0f;
    planner->move_direction = 1;
    planner->is_first_move_sample = false;
    planner->is_halted = false;
    planner->is_initialized = true;
    return STATUS_OK;
}

/**
 * @brief  设置普通闭环命令
 * @param  planner 规划器实例
 * @param  command 新的普通闭环命令
 * @retval STATUS_OK 命令已更新
 * @retval STATUS_INVALID_ARGUMENT 任一参数为空
 * @retval STATUS_NOT_INITIALIZED 规划器尚未初始化
 * @retval STATUS_OUT_OF_RANGE 命令不满足数值约束
 */
status_code_t motion_planner_set_normal_command(motion_planner_t *planner,
    const motion_planner_normal_command_t *command)
{
    if (!planner || !command) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!planner->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (!motion_planner_normal_command_is_valid(command)) {
        return STATUS_OUT_OF_RANGE;
    }

    planner->normal_command = *command;
    if (planner->state == MOTION_PLANNER_STATE_IDLE) {
        planner->command = motion_planner_command_from_normal(command);
    }
    return STATUS_OK;
}

/**
 * @brief  启动一次距离移动规划
 * @param  planner 规划器实例
 * @param  distance_mm 目标距离，单位：毫米，正值前进，负值后退
 * @param  speed 非零速度目标
 * @retval STATUS_OK 移动规划已启动
 * @retval STATUS_INVALID_ARGUMENT planner 为空
 * @retval STATUS_NOT_INITIALIZED 规划器尚未初始化
 * @retval STATUS_OUT_OF_RANGE 距离或速度无效，或换算结果溢出
 */
status_code_t motion_planner_start_move(motion_planner_t *planner, float distance_mm, float speed)
{
    float speed_magnitude;

    if (!planner) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!planner->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (!isfinite(distance_mm) || !isfinite(speed) || distance_mm == 0.0f || speed == 0.0f) {
        return STATUS_OUT_OF_RANGE;
    }

    speed_magnitude = motion_planner_abs(speed);
    planner->target_distance_counts =
        motion_planner_abs(distance_mm) * planner->config.encoder_counts_per_mm;
    if (!isfinite(planner->target_distance_counts)) {
        planner->target_distance_counts = 0.0f;
        return STATUS_OUT_OF_RANGE;
    }
    planner->accumulated_distance_counts = 0.0f;
    planner->move_direction = distance_mm < 0.0f ? -1 : 1;
    planner->move_speed = speed_magnitude;
    planner->is_first_move_sample = true;
    planner->is_halted = false;
    motion_planner_set_move_command(planner);
    planner->state = MOTION_PLANNER_STATE_MOVE;
    return STATUS_OK;
}

/**
 * @brief  启动一次相对航向旋转规划
 * @param  planner 规划器实例
 * @param  delta_angle_deg 相对当前目标的角度增量，单位：度
 * @param  speed 非零旋转基础速度目标
 * @retval STATUS_OK 旋转规划已启动
 * @retval STATUS_INVALID_ARGUMENT planner 为空
 * @retval STATUS_NOT_INITIALIZED 规划器尚未初始化
 * @retval STATUS_OUT_OF_RANGE 角度或速度不满足数值约束
 */
status_code_t motion_planner_start_rotate(motion_planner_t *planner, float delta_angle_deg,
    float speed)
{
    float current_target_angle_deg;

    if (!planner) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!planner->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (!isfinite(delta_angle_deg) || !isfinite(speed) || delta_angle_deg == 0.0f ||
        speed == 0.0f) {
        return STATUS_OUT_OF_RANGE;
    }
    if ((delta_angle_deg < -180.0f) || (delta_angle_deg > 180.0f)) {
        return STATUS_OUT_OF_RANGE;
    }

    current_target_angle_deg = planner->state == MOTION_PLANNER_STATE_ROTATE
                                   ? planner->rotate_target_angle_deg
                                   : planner->normal_command.target_angle_deg;
    planner->rotate_target_angle_deg =
        motion_planner_wrap_180(current_target_angle_deg + delta_angle_deg);
    planner->rotate_speed = speed;
    planner->is_halted = false;
    motion_planner_set_rotate_command(planner);
    planner->state = MOTION_PLANNER_STATE_ROTATE;
    return STATUS_OK;
}

/**
 * @brief  推进一次非阻塞规划
 * @param  planner 规划器实例
 * @param  feedback 本周期反馈快照
 * @param  command 接收控制回路命令的存储地址
 * @retval STATUS_OK 规划已推进且输出命令已写入
 * @retval STATUS_INVALID_ARGUMENT 任一参数为空
 * @retval STATUS_NOT_INITIALIZED 规划器尚未初始化
 * @retval STATUS_OUT_OF_RANGE 反馈包含非有限值或航向超出范围
 */
status_code_t motion_planner_step(motion_planner_t *planner,
    const motion_planner_feedback_t *feedback, motion_planner_command_t *command)
{
    float average_increment;
    float heading_error;

    if (!planner || !feedback || !command) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!planner->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (!isfinite(feedback->left_encoder_delta) || !isfinite(feedback->right_encoder_delta) ||
        !isfinite(feedback->actual_angle_deg) || feedback->actual_angle_deg < -180.0f ||
        feedback->actual_angle_deg > 180.0f) {
        return STATUS_OUT_OF_RANGE;
    }

    if (planner->is_halted) {
        planner->is_halted = false;
        *command = planner->command;
        return STATUS_OK;
    }

    switch (planner->state) {
        case MOTION_PLANNER_STATE_IDLE:
            break;

        case MOTION_PLANNER_STATE_MOVE:
            motion_planner_set_move_command(planner);
            if (planner->is_first_move_sample) {
                planner->is_first_move_sample = false;
                break;
            }
            average_increment = (motion_planner_abs(feedback->left_encoder_delta) +
                                    motion_planner_abs(feedback->right_encoder_delta)) *
                                0.5f;
            planner->accumulated_distance_counts += average_increment;
            if (planner->accumulated_distance_counts >= planner->target_distance_counts) {
                planner->state = MOTION_PLANNER_STATE_IDLE;
                planner->command = motion_planner_command_from_normal(&planner->normal_command);
            }
            break;

        case MOTION_PLANNER_STATE_ROTATE:
            motion_planner_set_rotate_command(planner);
            heading_error = motion_planner_wrap_180(planner->rotate_target_angle_deg -
                                                    feedback->actual_angle_deg);
            if (motion_planner_abs(heading_error) <= planner->config.rotate_dead_zone_deg) {
                planner->state = MOTION_PLANNER_STATE_IDLE;
                planner->normal_command.target_angle_deg = planner->rotate_target_angle_deg;
                planner->normal_command.is_angle_enabled = true;
                planner->command = motion_planner_command_from_normal(&planner->normal_command);
            }
            break;

        default:
            planner->state = MOTION_PLANNER_STATE_IDLE;
            planner->command = motion_planner_command_from_normal(&planner->normal_command);
            break;
    }

    *command = planner->command;
    return STATUS_OK;
}

/**
 * @brief  取消当前规划并进入普通闭环停止命令
 * @param  planner 规划器实例
 * @retval STATUS_OK 当前规划已取消
 * @retval STATUS_INVALID_ARGUMENT planner 为空
 * @retval STATUS_NOT_INITIALIZED 规划器尚未初始化
 */
status_code_t motion_planner_cancel(motion_planner_t *planner)
{
    if (!planner) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!planner->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }

    planner->state = MOTION_PLANNER_STATE_IDLE;
    planner->normal_command.base_speed = 0.0f;
    planner->normal_command.external_differential = 0.0f;
    planner->is_halted = false;
    motion_planner_set_stop_command(planner);
    return STATUS_OK;
}

/**
 * @brief  暂停输出但保留当前规划上下文
 * @param  planner 规划器实例
 * @retval STATUS_OK 已生成一个周期的安全停止命令
 * @retval STATUS_INVALID_ARGUMENT planner 为空
 * @retval STATUS_NOT_INITIALIZED 规划器尚未初始化
 */
status_code_t motion_planner_halt(motion_planner_t *planner)
{
    if (!planner) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!planner->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }

    motion_planner_set_stop_command(planner);
    planner->is_halted = true;
    return STATUS_OK;
}

/**
 * @brief  修改当前移动规划的剩余距离
 * @param  planner 规划器实例
 * @param  remaining_mm 新的剩余距离，单位：毫米，必须为正值
 * @retval STATUS_OK 剩余距离已更新
 * @retval STATUS_INVALID_ARGUMENT planner 为空
 * @retval STATUS_NOT_INITIALIZED 规划器尚未初始化
 * @retval STATUS_STATE_ERROR 当前未执行移动任务
 * @retval STATUS_OUT_OF_RANGE 剩余距离无效或换算结果溢出
 */
status_code_t motion_planner_replan_remaining(motion_planner_t *planner, float remaining_mm)
{
    float target_counts;

    if (!planner) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!planner->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (planner->state != MOTION_PLANNER_STATE_MOVE) {
        return STATUS_STATE_ERROR;
    }
    if (!isfinite(remaining_mm) || remaining_mm <= 0.0f) {
        return STATUS_OUT_OF_RANGE;
    }

    target_counts =
        planner->accumulated_distance_counts + remaining_mm * planner->config.encoder_counts_per_mm;
    if (!isfinite(target_counts)) {
        return STATUS_OUT_OF_RANGE;
    }
    planner->target_distance_counts = target_counts;
    return STATUS_OK;
}

/**
 * @brief  获取当前输出命令
 * @param  planner 规划器实例
 * @param  command 接收输出命令的存储地址
 * @retval STATUS_OK 命令已写入
 * @retval STATUS_INVALID_ARGUMENT 任一参数为空
 * @retval STATUS_NOT_INITIALIZED 规划器尚未初始化
 */
status_code_t motion_planner_get_command(const motion_planner_t *planner,
    motion_planner_command_t *command)
{
    if (!planner || !command) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!planner->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }

    *command = planner->command;
    return STATUS_OK;
}

/**
 * @brief  获取当前规划状态
 * @param  planner 规划器实例
 * @param  state 接收规划状态的存储地址
 * @retval STATUS_OK 状态已写入
 * @retval STATUS_INVALID_ARGUMENT 任一参数为空
 * @retval STATUS_NOT_INITIALIZED 规划器尚未初始化
 */
status_code_t motion_planner_get_state(const motion_planner_t *planner,
    motion_planner_state_t *state)
{
    if (!planner || !state) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!planner->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }

    *state = planner->state;
    return STATUS_OK;
}

/**
 * @brief  获取当前移动任务已经行驶的距离
 * @param  planner 规划器实例
 * @param  elapsed_mm 接收已移动距离的存储地址，单位：毫米
 * @retval STATUS_OK 距离已写入，非移动状态下写入零
 * @retval STATUS_INVALID_ARGUMENT 任一参数为空
 * @retval STATUS_NOT_INITIALIZED 规划器尚未初始化
 */
status_code_t motion_planner_get_elapsed_mm(const motion_planner_t *planner, float *elapsed_mm)
{
    if (!planner || !elapsed_mm) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!planner->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }

    if (planner->state != MOTION_PLANNER_STATE_MOVE) {
        *elapsed_mm = 0.0f;
    } else {
        *elapsed_mm = planner->accumulated_distance_counts / planner->config.encoder_counts_per_mm;
    }
    return STATUS_OK;
}

/**
 * @brief  获取当前移动任务尚未行驶的距离
 * @param  planner 规划器实例
 * @param  remaining_mm 接收剩余距离的存储地址，单位：毫米
 * @retval STATUS_OK 距离已写入，非移动状态下写入零
 * @retval STATUS_INVALID_ARGUMENT 任一参数为空
 * @retval STATUS_NOT_INITIALIZED 规划器尚未初始化
 */
status_code_t motion_planner_get_remaining_mm(const motion_planner_t *planner, float *remaining_mm)
{
    float remaining_counts;

    if (!planner || !remaining_mm) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!planner->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }

    if (planner->state != MOTION_PLANNER_STATE_MOVE) {
        *remaining_mm = 0.0f;
        return STATUS_OK;
    }
    remaining_counts = planner->target_distance_counts - planner->accumulated_distance_counts;
    if (remaining_counts < 0.0f) {
        remaining_counts = 0.0f;
    }
    *remaining_mm = remaining_counts / planner->config.encoder_counts_per_mm;
    return STATUS_OK;
}
