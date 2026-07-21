/**
 * @file    perception.c
 * @brief   摄像头目标、车辆偏航角和几何距离感知模块。
 * @author  mizuniuo01
 * @date    2026-07-22
 * @version 2.0.0
 * @note    目标偏差保持视觉端定义：目标在右侧和下方时分别为正。
 * @note    目标切换通过带请求 ID 的非阻塞事务完成。
 * @note    水平距离从摄像头光心起算，目标平面为地面。
 */

#include "perception.h"
#include "aim_config.h"
#include "cam.h"
#include "gyroscope.h"
#include "servo.h"

#include <math.h>
#include <string.h>
#include <stm32f4xx_hal.h>

/** @brief 摄像头事务和距离计算参数。 */
typedef enum {
    PERCEPTION_SWITCH_SEND_PERIOD_MS = 20U, /**< 切换命令重发周期。 */
    PERCEPTION_CAMERA_TIMEOUT_MS = 300U,    /**< 摄像头断流判定时间。 */
} perception_timing_value_t;

/** @brief 距离计算使用的浮点常量。 */
#define PERCEPTION_DEG_TO_RAD (3.14159265358979323846F / 180.0F)

/** @brief 可测量的最小向下俯角。 */
#define PERCEPTION_MIN_DOWN_PITCH_DEG 1.0F

/** @brief 可测量的最大向下俯角。 */
#define PERCEPTION_MAX_DOWN_PITCH_DEG 89.0F

/** @brief 目标切换事务状态。 */
typedef enum {
    PERCEPTION_SWITCH_STATE_IDLE = 0, /**< 没有切换事务。 */
    PERCEPTION_SWITCH_STATE_REQUESTING, /**< 持续发送切换请求。 */
    PERCEPTION_SWITCH_STATE_RESETTING,  /**< 发送 0 解除视觉端上升沿状态。 */
} perception_switch_state_t;

/** @brief 最近一份向上层发布的目标数据。 */
static perception_target_data_t perception_target_data;

/** @brief 非零表示目标数据尚未被上层消费。 */
static uint8_t perception_target_data_ready;

/** @brief 非零表示已经收到过至少一帧摄像头报告。 */
static uint8_t perception_camera_seen;

/** @brief 非零表示当前断流事件已经发布。 */
static uint8_t perception_camera_timeout_published;

/** @brief 最近一帧摄像头报告的系统时间。 */
static uint32_t perception_last_camera_tick_ms;

/** @brief 当前目标切换事务状态。 */
static perception_switch_state_t perception_switch_state;

/** @brief 当前目标切换请求 ID。 */
static uint8_t perception_switch_request_id;

/** @brief 最近一次切换命令成功入队的系统时间。 */
static uint32_t perception_last_switch_send_tick_ms;

/** @brief 目标切换成功事件锁存。 */
static uint8_t perception_switch_ack_latched;

/**
 * @brief  发布一份新的目标数据供上层消费。
 * @param  error_x     目标 X 偏差。
 * @param  error_y     目标 Y 偏差。
 * @param  has_target  是否存在目标。
 * @param  tick_ms     数据发布时间。
 * @return 无。
 */
static void perception_publish_target(int16_t error_x, int16_t error_y, uint8_t has_target,
    uint32_t tick_ms)
{
    perception_target_data.error_x = has_target ? error_x : 0;
    perception_target_data.error_y = has_target ? error_y : 0;
    perception_target_data.has_target = has_target ? 1U : 0U;
    perception_target_data.update_tick_ms = tick_ms;
    perception_target_data_ready = 1U;
}

/**
 * @brief  消费摄像头驱动发布的新目标报告。
 * @param  tick_ms  当前系统时间。
 * @return 无。
 */
static void perception_process_camera_data(uint32_t tick_ms)
{
    cam_data_t camera_data;
    uint8_t switch_ack_id;

    if (cam_take_data(&camera_data)) {
        perception_camera_seen = 1U;
        perception_camera_timeout_published = 0U;
        perception_last_camera_tick_ms = tick_ms;
        perception_publish_target(camera_data.error_x, camera_data.error_y,
            camera_data.has_target, tick_ms);
    }

    if ((perception_switch_state == PERCEPTION_SWITCH_STATE_REQUESTING)
        && cam_take_switch_ack(&switch_ack_id)
        && (switch_ack_id == perception_switch_request_id)) {
        perception_switch_ack_latched = 1U;
        perception_switch_state = PERCEPTION_SWITCH_STATE_RESETTING;
        perception_last_switch_send_tick_ms = tick_ms - PERCEPTION_SWITCH_SEND_PERIOD_MS;
    }
}

/**
 * @brief  在摄像头断流时发布一次无目标事件。
 * @param  tick_ms  当前系统时间。
 * @return 无。
 */
static void perception_process_camera_timeout(uint32_t tick_ms)
{
    if (!perception_camera_seen || perception_camera_timeout_published) {
        return;
    }
    if ((uint32_t)(tick_ms - perception_last_camera_tick_ms) < PERCEPTION_CAMERA_TIMEOUT_MS) {
        return;
    }

    perception_camera_timeout_published = 1U;
    perception_publish_target(0, 0, 0U, tick_ms);
}

/**
 * @brief  按周期推进目标切换发送事务。
 * @param  tick_ms  当前系统时间。
 * @return 无。
 */
static void perception_process_switch_transaction(uint32_t tick_ms)
{
    uint8_t switch_command;

    if (perception_switch_state == PERCEPTION_SWITCH_STATE_IDLE) {
        return;
    }
    if ((uint32_t)(tick_ms - perception_last_switch_send_tick_ms) <
        PERCEPTION_SWITCH_SEND_PERIOD_MS) {
        return;
    }

    switch_command = (perception_switch_state == PERCEPTION_SWITCH_STATE_REQUESTING) ? 1U : 0U;
    if (cam_send_switch_command(switch_command, perception_switch_request_id) != CAM_STATUS_OK) {
        return;
    }

    perception_last_switch_send_tick_ms = tick_ms;
    if (perception_switch_state == PERCEPTION_SWITCH_STATE_RESETTING) {
        perception_switch_state = PERCEPTION_SWITCH_STATE_IDLE;
    }
}

/**
 * @brief  把任意有限角度规范化到 0～360 度。
 * @param  angle_deg  原始角度。
 * @return 规范化后的角度；输入非有限数时返回 0。
 */
static float perception_normalize_angle_deg(float angle_deg)
{
    if (!isfinite(angle_deg)) {
        return 0.0F;
    }

    while (angle_deg < 0.0F) {
        angle_deg += 360.0F;
    }
    while (angle_deg >= 360.0F) {
        angle_deg -= 360.0F;
    }
    return angle_deg;
}

/**
 * @brief  初始化感知状态。
 * @return 无。
 */
void perception_init(void)
{
    memset(&perception_target_data, 0, sizeof(perception_target_data));
    perception_target_data_ready = 0U;
    perception_camera_seen = 0U;
    perception_camera_timeout_published = 0U;
    perception_last_camera_tick_ms = HAL_GetTick();
    perception_switch_state = PERCEPTION_SWITCH_STATE_IDLE;
    perception_switch_request_id = 0U;
    perception_last_switch_send_tick_ms = HAL_GetTick();
    perception_switch_ack_latched = 0U;
}

/**
 * @brief  处理摄像头报告、断流和目标切换事务。
 * @return 无。
 */
void perception_task(void)
{
    uint32_t tick_ms = HAL_GetTick();

    perception_process_camera_data(tick_ms);
    perception_process_camera_timeout(tick_ms);
    perception_process_switch_transaction(tick_ms);
}

/**
 * @brief  读取并消费一份新的目标数据。
 * @param  data  目标数据输出地址。
 * @return 有新数据返回 OK，无新数据返回 NOT_READY。
 */
perception_status_t perception_take_target_data(perception_target_data_t *data)
{
    if (data == NULL) {
        return PERCEPTION_STATUS_INVALID_ARGUMENT;
    }
    if (perception_target_data_ready == 0U) {
        return PERCEPTION_STATUS_NOT_READY;
    }

    *data = perception_target_data;
    perception_target_data_ready = 0U;
    return PERCEPTION_STATUS_OK;
}

/**
 * @brief  发起一次带非零请求 ID 的目标切换事务。
 * @return 已接受返回 OK，已有事务时返回 BUSY。
 */
perception_status_t perception_request_target_switch(void)
{
    uint32_t tick_ms;
    uint8_t stale_ack_id;

    if (perception_switch_state != PERCEPTION_SWITCH_STATE_IDLE) {
        return PERCEPTION_STATUS_BUSY;
    }

    perception_switch_request_id++;
    if (perception_switch_request_id == 0U) {
        perception_switch_request_id = 1U;
    }

    tick_ms = HAL_GetTick();
    (void)cam_take_switch_ack(&stale_ack_id);
    perception_switch_ack_latched = 0U;
    perception_switch_state = PERCEPTION_SWITCH_STATE_REQUESTING;
    perception_last_switch_send_tick_ms = tick_ms - PERCEPTION_SWITCH_SEND_PERIOD_MS;
    return PERCEPTION_STATUS_OK;
}

/**
 * @brief  判断目标切换事务是否尚未完全结束。
 * @return 请求或复位阶段返回 1，空闲返回 0。
 */
uint8_t perception_is_target_switch_pending(void)
{
    return (perception_switch_state == PERCEPTION_SWITCH_STATE_IDLE) ? 0U : 1U;
}

/**
 * @brief  读取并清除目标切换成功事件。
 * @return 自上次读取后成功过返回 1，否则返回 0。
 */
uint8_t perception_take_target_switch_ack(void)
{
    uint8_t ack = perception_switch_ack_latched;

    perception_switch_ack_latched = 0U;
    return ack;
}

/**
 * @brief  获取规范化到 0～360 度的车辆偏航角。
 * @param  yaw_deg  偏航角输出地址。
 * @return 已有合法陀螺仪数据返回 OK，否则返回对应状态。
 */
perception_status_t perception_get_vehicle_yaw_deg(float *yaw_deg)
{
    gyro_data_t gyro_data;

    if (yaw_deg == NULL) {
        return PERCEPTION_STATUS_INVALID_ARGUMENT;
    }
    if (!gyro_is_data_valid()) {
        return PERCEPTION_STATUS_NOT_READY;
    }

    gyro_data = gyro_get_data();
    *yaw_deg = perception_normalize_angle_deg(gyro_data.yaw);
    return PERCEPTION_STATUS_OK;
}

/**
 * @brief  计算 reference 到 current 的最短有符号偏航角差。
 * @param  reference_deg  参考角度。
 * @param  current_deg    当前角度。
 * @return 范围为 -180～+180 度的最短角差；非法输入返回 0。
 */
float perception_shortest_yaw_delta_deg(float reference_deg, float current_deg)
{
    float delta;

    if (!isfinite(reference_deg) || !isfinite(current_deg)) {
        return 0.0F;
    }

    delta =
        perception_normalize_angle_deg(current_deg) - perception_normalize_angle_deg(reference_deg);
    if (delta > 180.0F) {
        delta -= 360.0F;
    } else if (delta < -180.0F) {
        delta += 360.0F;
    }
    return delta;
}

/**
 * @brief  根据 Y 舵机绝对角计算镜头光心到地面目标点的水平距离。
 * @param  y_axis_angle_deg  当前 Y 舵机命令角度。
 * @param  distance_mm       水平距离输出地址，单位为毫米。
 * @return 成功、未标定、不可测量或参数错误状态。
 */
perception_status_t perception_calculate_horizontal_distance(float y_axis_angle_deg,
    float *distance_mm)
{
    float direction_sign;
    float down_pitch_deg;
    float link_angle_deg;
    float camera_height_mm;
    float distance;

    if ((distance_mm == NULL) || !isfinite(y_axis_angle_deg) ||
        (y_axis_angle_deg < SERVO_MIN_ANGLE_DEG) || (y_axis_angle_deg > SERVO_MAX_ANGLE_DEG)) {
        return PERCEPTION_STATUS_INVALID_ARGUMENT;
    }
    if ((AIM_GIMBAL_PIVOT_HEIGHT_MM <= 0.0F) || (AIM_CAMERA_LINK_LENGTH_MM <= 0.0F)) {
        return PERCEPTION_STATUS_NOT_CONFIGURED;
    }

    direction_sign = AIM_Y_POSITIVE_ANGLE_MOVES_DOWN ? 1.0F : -1.0F;
    down_pitch_deg = direction_sign * (y_axis_angle_deg - AIM_CAMERA_HORIZONTAL_Y_ANGLE_DEG);
    if ((down_pitch_deg < PERCEPTION_MIN_DOWN_PITCH_DEG) ||
        (down_pitch_deg > PERCEPTION_MAX_DOWN_PITCH_DEG)) {
        return PERCEPTION_STATUS_NOT_MEASURABLE;
    }

    link_angle_deg = AIM_CAMERA_LINK_REFERENCE_ANGLE_DEG - down_pitch_deg;
    camera_height_mm = AIM_GIMBAL_PIVOT_HEIGHT_MM +
                       AIM_CAMERA_LINK_LENGTH_MM * sinf(link_angle_deg * PERCEPTION_DEG_TO_RAD);
    if (!isfinite(camera_height_mm) || (camera_height_mm <= 0.0F)) {
        return PERCEPTION_STATUS_NOT_MEASURABLE;
    }

    distance = camera_height_mm / tanf(down_pitch_deg * PERCEPTION_DEG_TO_RAD);
    if (!isfinite(distance) || (distance <= 0.0F)) {
        return PERCEPTION_STATUS_NOT_MEASURABLE;
    }

    *distance_mm = distance;
    return PERCEPTION_STATUS_OK;
}
