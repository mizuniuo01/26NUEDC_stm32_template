/**
 * @file motion_planner.c
 * @brief 基于距离和航向的非阻塞运动规划领域实现。
 * @note 本模块不读取编码器/陀螺仪，也不写入电机；反馈和命令通过值对象交换。
 * @note 保留原工程的 MOVE/ROTATE/NORMAL 语义、首次采样跳过和角度死区。
 */
#include "motion_planner.h"

#include <math.h>
#include <stddef.h>

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

static float motion_planner_abs(float value)
{
    return value < 0.0f ? -value : value;
}

static uint8_t motion_planner_config_is_valid(const motion_planner_config_t *config)
{
    return (uint8_t)(config != NULL && isfinite(config->encoder_counts_per_mm) &&
                     isfinite(config->rotate_dead_zone_deg) &&
                     config->encoder_counts_per_mm > 0.0f &&
                     config->rotate_dead_zone_deg >= 0.0f);
}

static uint8_t motion_planner_normal_command_is_valid(
    const motion_planner_normal_command_t *command)
{
    return (uint8_t)(command != NULL && isfinite(command->base_speed) &&
                     isfinite(command->target_angle_deg) &&
                     isfinite(command->external_differential) &&
                     command->target_angle_deg >= -180.0f &&
                     command->target_angle_deg <= 180.0f);
}

static motion_planner_command_t motion_planner_command_from_normal(
    const motion_planner_normal_command_t *normal_command)
{
    motion_planner_command_t command = {
        .base_speed = normal_command->base_speed,
        .target_angle_deg = normal_command->target_angle_deg,
        .external_differential = normal_command->external_differential,
        .is_angle_enabled = normal_command->is_angle_enabled,
    };

    return command;
}

static void motion_planner_set_stop_command(motion_planner_t *planner)
{
    planner->command.base_speed = 0.0f;
    planner->command.external_differential = 0.0f;
    planner->command.is_angle_enabled = planner->normal_command.is_angle_enabled;
}

static void motion_planner_set_move_command(motion_planner_t *planner)
{
    planner->command = motion_planner_command_from_normal(&planner->normal_command);
    planner->command.base_speed = (float)planner->move_direction * planner->move_speed;
    planner->command.external_differential = 0.0f;
    planner->command.is_angle_enabled = 1U;
}

static void motion_planner_set_rotate_command(motion_planner_t *planner)
{
    planner->command = motion_planner_command_from_normal(&planner->normal_command);
    planner->command.base_speed = planner->rotate_speed;
    planner->command.target_angle_deg = planner->rotate_target_angle_deg;
    planner->command.external_differential = 0.0f;
    planner->command.is_angle_enabled = 1U;
}

/**
 * @brief 初始化运动规划器。
 * @param planner 规划器实例。
 * @param config 换算和完成判定配置。
 * @param normal_command 默认普通闭环命令。
 * @return 初始化状态。
 */
status_code_t motion_planner_init(
    motion_planner_t *planner,
    const motion_planner_config_t *config,
    const motion_planner_normal_command_t *normal_command)
{
    if ((planner == NULL) || (config == NULL) || (normal_command == NULL)) {
        return STATUS_INVALID_ARGUMENT;
    }
    if ((motion_planner_config_is_valid(config) == 0U) ||
        (motion_planner_normal_command_is_valid(normal_command) == 0U)) {
        return STATUS_OUT_OF_RANGE;
    }

    planner->is_initialized = 0U;
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
    planner->is_first_move_sample = 0U;
    planner->is_halted = 0U;
    planner->is_initialized = 1U;
    return STATUS_OK;
}

/**
 * @brief 设置普通闭环命令。
 * @param planner 规划器实例。
 * @param command 新的普通闭环命令。
 * @return 设置状态。
 */
status_code_t motion_planner_set_normal_command(
    motion_planner_t *planner,
    const motion_planner_normal_command_t *command)
{
    if ((planner == NULL) || (command == NULL)) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (planner->is_initialized == 0U) {
        return STATUS_NOT_INITIALIZED;
    }
    if (motion_planner_normal_command_is_valid(command) == 0U) {
        return STATUS_OUT_OF_RANGE;
    }

    planner->normal_command = *command;
    if (planner->state == MOTION_PLANNER_STATE_IDLE) {
        planner->command = motion_planner_command_from_normal(command);
    }
    return STATUS_OK;
}

/**
 * @brief 启动一次距离移动规划。
 * @param planner 规划器实例。
 * @param distance_mm 目标距离，正值前进，负值后退。
 * @param speed 速度目标的绝对值，必须非零。
 * @return 启动状态。
 */
status_code_t motion_planner_start_move(
    motion_planner_t *planner,
    float distance_mm,
    float speed)
{
    float speed_magnitude;

    if (planner == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (planner->is_initialized == 0U) {
        return STATUS_NOT_INITIALIZED;
    }
    if (!isfinite(distance_mm) || !isfinite(speed) || distance_mm == 0.0f ||
        speed == 0.0f) {
        return STATUS_OUT_OF_RANGE;
    }

    speed_magnitude = motion_planner_abs(speed);
    planner->target_distance_counts = motion_planner_abs(distance_mm) *
                                      planner->config.encoder_counts_per_mm;
    if (!isfinite(planner->target_distance_counts)) {
        planner->target_distance_counts = 0.0f;
        return STATUS_OUT_OF_RANGE;
    }
    planner->accumulated_distance_counts = 0.0f;
    planner->move_direction = distance_mm < 0.0f ? -1 : 1;
    planner->move_speed = speed_magnitude;
    planner->is_first_move_sample = 1U;
    planner->is_halted = 0U;
    motion_planner_set_move_command(planner);
    planner->state = MOTION_PLANNER_STATE_MOVE;
    return STATUS_OK;
}

/**
 * @brief 启动一次相对航向旋转规划。
 * @param planner 规划器实例。
 * @param delta_angle_deg 相对当前普通目标的角度增量。
 * @param speed 旋转时的基础速度目标。
 * @return 启动状态。
 */
status_code_t motion_planner_start_rotate(
    motion_planner_t *planner,
    float delta_angle_deg,
    float speed)
{
    float current_target_angle_deg;

    if (planner == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (planner->is_initialized == 0U) {
        return STATUS_NOT_INITIALIZED;
    }
    if (!isfinite(delta_angle_deg) || !isfinite(speed) ||
        delta_angle_deg == 0.0f || speed == 0.0f) {
        return STATUS_OUT_OF_RANGE;
    }
    if ((delta_angle_deg < -180.0f) || (delta_angle_deg > 180.0f)) {
        return STATUS_OUT_OF_RANGE;
    }

    current_target_angle_deg = planner->state == MOTION_PLANNER_STATE_ROTATE ?
                               planner->rotate_target_angle_deg :
                               planner->normal_command.target_angle_deg;
    planner->rotate_target_angle_deg = motion_planner_wrap_180(
        current_target_angle_deg + delta_angle_deg);
    planner->rotate_speed = speed;
    planner->is_halted = 0U;
    motion_planner_set_rotate_command(planner);
    planner->state = MOTION_PLANNER_STATE_ROTATE;
    return STATUS_OK;
}

/**
 * @brief 推进一次非阻塞规划。
 * @param planner 规划器实例。
 * @param feedback 本周期反馈快照。
 * @param command 输出给控制回路的命令。
 * @return 推进状态。
 */
status_code_t motion_planner_step(
    motion_planner_t *planner,
    const motion_planner_feedback_t *feedback,
    motion_planner_command_t *command)
{
    float average_increment;
    float heading_error;

    if ((planner == NULL) || (feedback == NULL) || (command == NULL)) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (planner->is_initialized == 0U) {
        return STATUS_NOT_INITIALIZED;
    }
    if (!isfinite(feedback->left_encoder_delta) ||
        !isfinite(feedback->right_encoder_delta) ||
        !isfinite(feedback->actual_angle_deg) ||
        feedback->actual_angle_deg < -180.0f ||
        feedback->actual_angle_deg > 180.0f) {
        return STATUS_OUT_OF_RANGE;
    }

    if (planner->is_halted != 0U) {
        planner->is_halted = 0U;
        *command = planner->command;
        return STATUS_OK;
    }

    switch (planner->state) {
        case MOTION_PLANNER_STATE_IDLE:
            break;

        case MOTION_PLANNER_STATE_MOVE:
            motion_planner_set_move_command(planner);
            if (planner->is_first_move_sample != 0U) {
                planner->is_first_move_sample = 0U;
                break;
            }
            average_increment = (motion_planner_abs(feedback->left_encoder_delta) +
                                 motion_planner_abs(feedback->right_encoder_delta)) *
                                0.5f;
            planner->accumulated_distance_counts += average_increment;
            if (planner->accumulated_distance_counts >=
                planner->target_distance_counts) {
                planner->state = MOTION_PLANNER_STATE_IDLE;
                planner->command = motion_planner_command_from_normal(
                    &planner->normal_command);
            }
            break;

        case MOTION_PLANNER_STATE_ROTATE:
            motion_planner_set_rotate_command(planner);
            heading_error = motion_planner_wrap_180(
                planner->rotate_target_angle_deg - feedback->actual_angle_deg);
            if (motion_planner_abs(heading_error) <=
                planner->config.rotate_dead_zone_deg) {
                planner->state = MOTION_PLANNER_STATE_IDLE;
                planner->normal_command.target_angle_deg =
                    planner->rotate_target_angle_deg;
                planner->normal_command.is_angle_enabled = 1U;
                planner->command = motion_planner_command_from_normal(
                    &planner->normal_command);
            }
            break;

        default:
            planner->state = MOTION_PLANNER_STATE_IDLE;
            planner->command = motion_planner_command_from_normal(
                &planner->normal_command);
            break;
    }

    *command = planner->command;
    return STATUS_OK;
}

/**
 * @brief 取消当前规划并进入普通闭环停止命令。
 * @param planner 规划器实例。
 * @return 取消状态。
 */
status_code_t motion_planner_cancel(motion_planner_t *planner)
{
    if (planner == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (planner->is_initialized == 0U) {
        return STATUS_NOT_INITIALIZED;
    }

    planner->state = MOTION_PLANNER_STATE_IDLE;
    planner->normal_command.base_speed = 0.0f;
    planner->normal_command.external_differential = 0.0f;
    planner->is_halted = 0U;
    motion_planner_set_stop_command(planner);
    return STATUS_OK;
}

/**
 * @brief 暂停输出但保留当前规划上下文。
 * @param planner 规划器实例。
 * @return 停止状态。
 */
status_code_t motion_planner_halt(motion_planner_t *planner)
{
    if (planner == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (planner->is_initialized == 0U) {
        return STATUS_NOT_INITIALIZED;
    }

    motion_planner_set_stop_command(planner);
    planner->is_halted = 1U;
    return STATUS_OK;
}

/**
 * @brief 修改当前移动规划的剩余距离。
 * @param planner 规划器实例。
 * @param remaining_mm 新的剩余距离，必须为正值。
 * @return 修改状态。
 */
status_code_t motion_planner_replan_remaining(
    motion_planner_t *planner,
    float remaining_mm)
{
    float target_counts;

    if (planner == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (planner->is_initialized == 0U) {
        return STATUS_NOT_INITIALIZED;
    }
    if (planner->state != MOTION_PLANNER_STATE_MOVE) {
        return STATUS_STATE_ERROR;
    }
    if (!isfinite(remaining_mm) || remaining_mm <= 0.0f) {
        return STATUS_OUT_OF_RANGE;
    }

    target_counts = planner->accumulated_distance_counts +
                    remaining_mm * planner->config.encoder_counts_per_mm;
    if (!isfinite(target_counts)) {
        return STATUS_OUT_OF_RANGE;
    }
    planner->target_distance_counts = target_counts;
    return STATUS_OK;
}

/**
 * @brief 获取当前命令。
 * @param planner 规划器实例。
 * @param command 输出命令。
 * @return 查询状态。
 */
status_code_t motion_planner_get_command(
    const motion_planner_t *planner,
    motion_planner_command_t *command)
{
    if ((planner == NULL) || (command == NULL)) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (planner->is_initialized == 0U) {
        return STATUS_NOT_INITIALIZED;
    }

    *command = planner->command;
    return STATUS_OK;
}

/**
 * @brief 获取规划状态。
 * @param planner 规划器实例。
 * @param state 输出状态。
 * @return 查询状态。
 */
status_code_t motion_planner_get_state(
    const motion_planner_t *planner,
    motion_planner_state_t *state)
{
    if ((planner == NULL) || (state == NULL)) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (planner->is_initialized == 0U) {
        return STATUS_NOT_INITIALIZED;
    }

    *state = planner->state;
    return STATUS_OK;
}

/**
 * @brief 获取已移动距离。
 * @param planner 规划器实例。
 * @param elapsed_mm 输出距离，单位：毫米。
 * @return 查询状态。
 */
status_code_t motion_planner_get_elapsed_mm(
    const motion_planner_t *planner,
    float *elapsed_mm)
{
    if ((planner == NULL) || (elapsed_mm == NULL)) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (planner->is_initialized == 0U) {
        return STATUS_NOT_INITIALIZED;
    }

    if (planner->state != MOTION_PLANNER_STATE_MOVE) {
        *elapsed_mm = 0.0f;
    } else {
        *elapsed_mm = planner->accumulated_distance_counts /
                      planner->config.encoder_counts_per_mm;
    }
    return STATUS_OK;
}

/**
 * @brief 获取剩余移动距离。
 * @param planner 规划器实例。
 * @param remaining_mm 输出距离，单位：毫米。
 * @return 查询状态。
 */
status_code_t motion_planner_get_remaining_mm(
    const motion_planner_t *planner,
    float *remaining_mm)
{
    float remaining_counts;

    if ((planner == NULL) || (remaining_mm == NULL)) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (planner->is_initialized == 0U) {
        return STATUS_NOT_INITIALIZED;
    }

    if (planner->state != MOTION_PLANNER_STATE_MOVE) {
        *remaining_mm = 0.0f;
        return STATUS_OK;
    }
    remaining_counts = planner->target_distance_counts -
                       planner->accumulated_distance_counts;
    if (remaining_counts < 0.0f) {
        remaining_counts = 0.0f;
    }
    *remaining_mm = remaining_counts / planner->config.encoder_counts_per_mm;
    return STATUS_OK;
}
