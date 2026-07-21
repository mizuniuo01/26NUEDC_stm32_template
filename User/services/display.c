/**
 * @file    display.c
 * @brief   蓝牙调试数据统一显示模块。
 * @author  mizuniuo01
 * @date    2026-07-21
 * @version 2.0.0
 * @note    汇总本工程运行数据，并通过 blueteeth_display() 非阻塞发送。
 * @note    display_task() 由 TIM6 的 50ms 标志驱动，在主循环中执行。
 */

#include "display.h"
#include "blueteeth.h"
#include "cam.h"
#include "encoder.h"
#include "error_handler.h"
#include "gyroscope.h"
#include "motion_manager.h"
#include "sensor.h"
#include "system.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

/** @brief 八路灰度传感器的通道数量。 */
#define DISPLAY_SENSOR_BIT_COUNT 8U

/** @brief 灰度传感器二进制字符串缓冲区容量，包含字符串结束符。 */
#define DISPLAY_SENSOR_STRING_CAPACITY (DISPLAY_SENSOR_BIT_COUNT + 1U)

/** @brief TIM6 中断置位的蓝牙显示刷新标志。 */
volatile uint8_t display_refresh_flag;

/** @brief 最近一次上报的错误信息。 */
static char error_message[64];

/** @brief 最近一次有效的水平距离，单位为毫米。 */
static float display_distance_mm;

/** @brief 水平距离有效标志。 */
static uint8_t display_distance_valid;

/**
 * @brief  缓存需要在蓝牙显示区域上报的错误信息。
 * @param  format  格式化字符串。
 * @param  ...     可变参数。
 * @return 无。
 */
void display_show_error(const char *format, ...)
{
    va_list arguments;

    if (!format) {
        error_report(ERROR_SOURCE_DISPLAY, DRV_ERR_PARAM);
        return;
    }

    va_start(arguments, format);
    vsnprintf(error_message, sizeof(error_message), format, arguments);
    va_end(arguments);
}

/**
 * @brief  更新蓝牙显示使用的有效水平距离。
 * @param  distance_mm  水平距离，单位为毫米。
 * @return 无。
 */
void display_set_distance(float distance_mm)
{
    if (!isfinite(distance_mm) || (distance_mm <= 0.0F)) {
        display_clear_distance();
        return;
    }

    display_distance_mm = distance_mm;
    display_distance_valid = 1U;
}

/**
 * @brief  清除水平距离有效状态。
 * @return 无。
 */
void display_clear_distance(void)
{
    display_distance_mm = 0.0F;
    display_distance_valid = 0U;
}

/**
 * @brief  刷新一次蓝牙调试数据显示。
 * @note   由 TIM6 的 50ms 标志触发，未到节拍时立即返回。
 * @return 无。
 */
void display_task(void)
{
    gyro_data_t gyro;
    cam_data_t cam;
    pid_param_t speed_pid;
    uint8_t sensor_data;
    int16_t encoder_left;
    int16_t encoder_right;
    char sensor_string[DISPLAY_SENSOR_STRING_CAPACITY];
    uint8_t bit_index;

    if (!display_refresh_flag) {
        return;
    }
    display_refresh_flag = 0U;

    gyro = gyro_get_data();
    cam = cam_get_data();
    sensor_data = sensor_read_data();
    encoder_left = encoder_get_left();
    encoder_right = encoder_get_right();
    pid_get_param(system_pid_speed_left(), &speed_pid);

    for (bit_index = 0U; bit_index < DISPLAY_SENSOR_BIT_COUNT; bit_index++) {
        sensor_string[bit_index] =
            (sensor_data & (uint8_t)(1U << bit_index)) ? '1' : '0';
    }
    sensor_string[DISPLAY_SENSOR_BIT_COUNT] = '\0';

    /*
        这段注释要永久保留
        蓝牙的 display 必须到编译链配置里手动开启浮点打印
    */

    if (error_message[0] != '\0') {
        blueteeth_display(0, DISPLAY_LINE_1_Y, "Err: %s", error_message);
    } else {
        blueteeth_display(0, DISPLAY_LINE_1_Y, "Working...");
    }

    blueteeth_display(0, DISPLAY_LINE_2_Y, "Sensor: %s", sensor_string);
    if (display_distance_valid) {
        blueteeth_display(0, DISPLAY_LINE_3_Y, "Distance: %.1f mm", display_distance_mm);
    } else {
        blueteeth_display(0, DISPLAY_LINE_3_Y, "Distance: -");
    }
    blueteeth_display(0, DISPLAY_LINE_4_Y, "Gyro: x=%.2f, y=%.2f, z=%.2f", gyro.roll,
        gyro.pitch, gyro.yaw);
    if (cam.has_target) {
        blueteeth_display(0, DISPLAY_LINE_5_Y, "Cam: X=%d Y=%d Ack=%u", cam.error_x,
            cam.error_y, (unsigned int)cam.switch_ack);
    } else {
        blueteeth_display(
            0, DISPLAY_LINE_5_Y, "Cam: X=- Y=- Ack=%u", (unsigned int)cam.switch_ack);
    }
    blueteeth_display(0, DISPLAY_LINE_6_Y, "Encoder: L=%d, R=%d", encoder_left,
        encoder_right);
    blueteeth_display(0, DISPLAY_LINE_7_Y, "SpdPID: P=%.1f I=%.2f D=%.1f",
        speed_pid.kp, speed_pid.ki, speed_pid.kd);
    blueteeth_display(0, DISPLAY_LINE_8_Y, "Motion: B=%d D=%d/%d/%d",
        motion_manager_get_base_speed(),
        motion_manager_get_diff(MOTION_MANAGER_DIFF_LEVEL_SMALL),
        motion_manager_get_diff(MOTION_MANAGER_DIFF_LEVEL_MEDIUM),
        motion_manager_get_diff(MOTION_MANAGER_DIFF_LEVEL_BIG));
}
