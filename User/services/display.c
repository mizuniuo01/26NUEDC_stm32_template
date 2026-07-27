/**
 * @file display.c
 * @brief 周期读取板级快照并集中刷新蓝牙诊断界面。
 */
#include "display.h"
#include "bluetooth_service.h"
#include "bsp_board.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define DISPLAY_REFRESH_PERIOD_MS 50U /* 蓝牙诊断界面刷新周期，单位：毫秒 */
#define DISPLAY_STATUS_CAPACITY 32U /* 运行状态文本容量，单位：字节 */

static char status_message[DISPLAY_STATUS_CAPACITY];
static uint16_t command_count;
static uint8_t key_state;
static uint8_t key_pressed_events;
static uint32_t next_refresh_ms;
static float speed_target;
static float left_speed_output;
static float right_speed_output;
static bool is_speed_control_active;
static bool is_initialized;

/**
 * @brief  初始化集中式蓝牙诊断显示
 * @return 无
 */
void display_init(void)
{
    (void)snprintf(status_message, sizeof(status_message), "Ready");
    command_count = 0U;
    key_state = 0U;
    key_pressed_events = 0U;
    next_refresh_ms = bsp_time_get_ms();
    speed_target = 0.0F;
    left_speed_output = 0.0F;
    right_speed_output = 0.0F;
    is_speed_control_active = false;
    is_initialized = true;
    (void)bluetooth_service_clear_display();
}

/**
 * @brief  按固定周期读取最新快照并刷新蓝牙显示和绘图
 * @retval STATUS_OK 未到刷新周期或本次刷新数据已入队
 * @retval STATUS_NOT_INITIALIZED 显示服务尚未初始化
 * @retval STATUS_BUSY 蓝牙服务发送队列空间不足
 * @retval STATUS_OUT_OF_RANGE 格式化后的协议帧超过容量
 */
status_code_t display_process(void)
{
    bsp_gyro_snapshot_t gyro;
    bsp_camera_snapshot_t camera;
    bsp_feedback_snapshot_t feedback;
    status_code_t status;
    uint32_t now_ms;
    uint32_t missed_periods;
    uint16_t distance_mm;
    bool is_distance_valid;

    if (!is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    now_ms = bsp_time_get_ms();
    if ((int32_t)(now_ms - next_refresh_ms) < 0) {
        return STATUS_OK;
    }
    missed_periods = (now_ms - next_refresh_ms) / DISPLAY_REFRESH_PERIOD_MS;
    next_refresh_ms += (missed_periods + 1U) * DISPLAY_REFRESH_PERIOD_MS;

    status = bluetooth_service_display(0, DISPLAY_LINE_STATUS_Y, "%s t=%lu.%lus",
        status_message, (unsigned long)(now_ms / 1000U),
        (unsigned long)((now_ms % 1000U) / 100U));
    if (status != STATUS_OK) {
        return status;
    }
    status = bsp_gyro_snapshot(&gyro);
    if ((status == STATUS_OK) && gyro.is_valid) {
        status = bluetooth_service_display(0, DISPLAY_LINE_GYRO_Y,
            "Gyro: %.2f %.2f %.2f #%lu", (double)gyro.roll, (double)gyro.pitch,
            (double)gyro.yaw, (unsigned long)gyro.sequence);
        if (status != STATUS_OK) {
            return status;
        }
    } else {
        status = bluetooth_service_display(0, DISPLAY_LINE_GYRO_Y, "Gyro: invalid");
        if (status != STATUS_OK) {
            return status;
        }
    }
    status = bsp_feedback_get(&feedback);
    if (status == STATUS_OK) {
        status = bluetooth_service_display(0, DISPLAY_LINE_ENCODER_Y,
            "Encoder: L=%ld R=%ld #%lu", (long)feedback.left_delta,
            (long)feedback.right_delta, (unsigned long)feedback.sequence);
        if (status != STATUS_OK) {
            return status;
        }
        status = bluetooth_service_plot("%.1f,%ld,%ld", (double)speed_target,
            (long)feedback.left_delta, (long)feedback.right_delta);
    } else {
        status = bluetooth_service_display(0, DISPLAY_LINE_ENCODER_Y, "Encoder: invalid");
    }
    if (status != STATUS_OK) {
        return status;
    }
    status = bluetooth_service_display(0, DISPLAY_LINE_SPEED_Y,
        "Speed: %s T=%.1f PWM=%.0f/%.0f", is_speed_control_active ? "ON" : "OFF",
        (double)speed_target, (double)left_speed_output, (double)right_speed_output);
    if (status != STATUS_OK) {
        return status;
    }
    status = bluetooth_service_display(0, DISPLAY_LINE_COMMAND_Y, "Commands: %u",
        (unsigned int)command_count);
    if (status != STATUS_OK) {
        return status;
    }
    status = bluetooth_service_display(0, DISPLAY_LINE_KEYS_Y, "Keys: state=%02X press=%02X",
        (unsigned int)key_state, (unsigned int)key_pressed_events);
    if (status == STATUS_OK) {
        key_pressed_events = 0U;
    }
    if (status != STATUS_OK) {
        return status;
    }
    status = bsp_ultrasonic_get(&distance_mm, &is_distance_valid);
    if (status != STATUS_OK) {
        return bluetooth_service_display(0, DISPLAY_LINE_ULTRASONIC_Y, "Ultra: unavailable");
    }
    status = bluetooth_service_display(0, DISPLAY_LINE_ULTRASONIC_Y, "Ultra: %u mm valid=%u",
        (unsigned int)distance_mm, is_distance_valid ? 1U : 0U);
    if (status != STATUS_OK) {
        return status;
    }
    status = bsp_camera_snapshot(&camera);
    if (status != STATUS_OK) {
        return bluetooth_service_display(0, DISPLAY_LINE_CAMERA_Y, "Camera: unavailable");
    }
    return bluetooth_service_display(0, DISPLAY_LINE_CAMERA_Y,
        "Camera: T=%u X=%d Y=%d ACK=%u/%u #%lu", camera.has_target ? 1U : 0U,
        (int)camera.error_x, (int)camera.error_y, camera.has_switch_ack ? 1U : 0U,
        (unsigned int)camera.switch_ack_id, (unsigned long)camera.sequence);
}

/**
 * @brief  更新由显示服务周期输出的状态文本
 * @param  message 以空字符结尾的状态文本
 * @retval STATUS_OK 状态文本已更新
 * @retval STATUS_INVALID_ARGUMENT message 为空
 * @retval STATUS_OUT_OF_RANGE message 超过状态文本容量
 */
status_code_t display_set_status(const char *message)
{
    int result;

    if (!message) {
        return STATUS_INVALID_ARGUMENT;
    }
    result = snprintf(status_message, sizeof(status_message), "%s", message);
    if (result < 0) {
        return STATUS_INVALID_ARGUMENT;
    }
    return (size_t)result < sizeof(status_message) ? STATUS_OK : STATUS_OUT_OF_RANGE;
}

/**
 * @brief  更新由显示服务周期输出的蓝牙命令累计数量
 * @param  count 已接收的命令数量
 * @return 无
 */
void display_set_command_count(uint16_t count)
{
    command_count = count;
}

/**
 * @brief  更新按键稳定状态并累积尚未显示的按下事件
 * @param  state 当前稳定按下状态位掩码
 * @param  pressed_events 本次取到的按下事件位掩码
 * @return 无
 */
void display_update_keys(uint8_t state, uint8_t pressed_events)
{
    key_state = state;
    key_pressed_events |= pressed_events;
}

/**
 * @brief  更新由显示服务周期输出的速度闭环状态
 * @param  target 双轮速度目标，单位：count/10ms
 * @param  left_output 左轮 PWM 输出
 * @param  right_output 右轮 PWM 输出
 * @param  is_active 速度闭环运行标志
 * @return 无
 */
void display_set_speed_control(float target, float left_output, float right_output,
    bool is_active)
{
    speed_target = target;
    left_speed_output = left_output;
    right_speed_output = right_output;
    is_speed_control_active = is_active;
}
