/**
 * @file    aim_control.c
 * @brief   自动瞄准应用调度层。
 * @author  mizuniuo01
 * @date    2026-07-22
 * @version 1.0.0
 * @note    本层只编排 perception 与 aim_tools，不直接访问 UART 或舵机协议。
 * @note    当前策略为有目标时逐视觉帧闭环，无目标或摄像头断流时归位。
 * @note    仅在目标存在且 X/Y 原始偏差均位于正负 3 像素死区时发布有效距离。
 */

#include "aim_control.h"
#include "aim_tools.h"
#include "display.h"
#include "perception.h"

#include <stddef.h>

/** @brief 瞄准调度层初始化完成标志。 */
static uint8_t aim_control_initialized;

/**
 * @brief  将瞄准工具状态转换为调度层状态。
 * @param  status  瞄准工具状态。
 * @return 对应调度层状态。
 */
static aim_control_status_t aim_control_map_tools_status(aim_tools_status_t status)
{
    if (status == AIM_TOOLS_STATUS_OK) {
        return AIM_CONTROL_STATUS_OK;
    }
    if (status == AIM_TOOLS_STATUS_NOT_INITIALIZED) {
        return AIM_CONTROL_STATUS_NOT_INITIALIZED;
    }
    return AIM_CONTROL_STATUS_IO_ERROR;
}

/**
 * @brief  根据本帧目标状态更新蓝牙显示使用的距离。
 * @param  target_data  本帧目标数据。
 * @return 无。
 * @note   中心判定与云台 PID 共用 aim_tools 的正负 3 像素死区。
 */
static void aim_control_update_distance(const perception_target_data_t *target_data)
{
    float y_axis_angle_deg;
    float distance_mm;

    if (!target_data->has_target ||
        !aim_tools_target_is_centered(target_data->error_x, target_data->error_y)) {
        display_clear_distance();
        return;
    }

    aim_tools_get_gimbal_angles(NULL, &y_axis_angle_deg);
    if (perception_calculate_horizontal_distance(y_axis_angle_deg, &distance_mm) ==
        PERCEPTION_STATUS_OK) {
        display_set_distance(distance_mm);
    } else {
        display_clear_distance();
    }
}

/**
 * @brief  初始化感知层、云台 HOME 位置和扳机静止位置。
 * @return 初始化状态。
 */
aim_control_status_t aim_control_init(void)
{
    aim_tools_status_t status;

    perception_init();
    display_clear_distance();
    status = aim_tools_init();
    aim_control_initialized = (status == AIM_TOOLS_STATUS_OK) ? 1U : 0U;
    return aim_control_map_tools_status(status);
}

/**
 * @brief  推进感知事务、射击状态机和每帧自动云台闭环。
 * @return 无。
 */
void aim_control_task(void)
{
    perception_target_data_t target_data;

    if (!aim_control_initialized) {
        return;
    }

    perception_task();
    aim_tools_task();
    if (perception_take_target_data(&target_data) != PERCEPTION_STATUS_OK) {
        return;
    }

    if (target_data.has_target) {
        (void)aim_tools_track_target(target_data.error_x, target_data.error_y);
        aim_control_update_distance(&target_data);
    } else {
        display_clear_distance();
        (void)aim_tools_return_home();
    }
}
