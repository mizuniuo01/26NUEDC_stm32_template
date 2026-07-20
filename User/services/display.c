/**
 * @file    display.c
 * @brief   蓝牙调试仪表盘模块（数据汇总显示）
 * @author  mizuniuo01
 * @date    2026-05-25
 * @version 1.0.0
 * @note    通过 blueteeth_display() 输出到江协科技蓝牙串口小程序
 * @note    依赖：blueteeth 模块
 *
 * @usage
 * ─────────────────────────────────────────────────────────
 * 汇总各模块数据，通过蓝牙屏显输出。display_refresh_flag
 * 由定时器 ISR 周期性置位，display_task 在主循环中调用。
 *
 * ── ISR 中 ──
 *
 * void Timer_IRQHandler(void)
 * {
 *     display_refresh_flag = 1;
 * }
 *
 * ── 主循环 ──
 *
 * display_task();
 *
 * ── 各模块错误上报 ──
 *
 * display_show_error("motor: stuck");
 */

#include "display.h"
#include "error_handler.h"
#include "blueteeth.h"
#include "gyroscope.h"
#include "sensor.h"
#include "cam.h"
#include "encoder.h"
#include "system.h"
#include "motion_manager.h"
#include "motion_control.h"
#include "perception.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define SENSOR_STR_BUF_SIZE 30 /* 传感器字符串缓冲区大小 */
#define SENSOR_BIT_COUNT 8     /* 传感器位数 */

volatile uint8_t display_refresh_flag;

/* 错误信息缓冲 */
static char error_msg[64];

/**
 * @brief  显示单个无刷电机的缓存反馈数据。
 * @param  line_y  蓝牙屏显行的 Y 坐标。
 * @param  axis    电机轴名称。
 * @param  motor   无刷电机实例。
 * @return 无。
 */
static void display_bldc_telemetry(int16_t line_y, const char *axis, const bldc_motor_t *motor)
{
    bldc_telemetry_t telemetry;

    if (bldc_get_telemetry(motor, &telemetry) != BLDC_STATUS_OK) {
        blueteeth_display(0, line_y, "BLDC %s: waiting...", axis);
        return;
    }

    blueteeth_display(0, line_y, "BLDC %s: S=%ld T=%.1f M=%.1f A=%.1f V=%.2f", axis,
        (long)telemetry.speed_rpm, telemetry.total_angle_deg, telemetry.mechanical_angle_deg,
        telemetry.acceleration_rps2, telemetry.bus_voltage_v);
}

/**
 * @brief  上报错误信息
 * @param  format  格式化字符串
 * @param  ...     可变参数
 * @retval 无
 */
void display_show_error(const char *format, ...)
{
    va_list args;

    if (!format) {
        error_report(ERROR_SOURCE_DISPLAY, DRV_ERR_PARAM);
        return;
    }

    va_start(args, format);
    vsnprintf(error_msg, sizeof(error_msg), format, args);
    va_end(args);
}

/**
 * @brief  显示刷新任务（主循环中调用）
 * @note   由 display_refresh_flag 触发
 * @param  无
 * @retval 无
 */
void display_task(void)
{
    gyro_data_t gyro;
    uint8_t sensor_data;
    cam_data_t cam;
    int16_t encoder_left;
    int16_t encoder_right;
    pid_param_t sp_pid;
    pid_param_t ap_pid;
    float target_angle;
    const perception_data_t *perception;
    char sensor_str[SENSOR_STR_BUF_SIZE];
    int i;

    if (!display_refresh_flag) {
        return;
    }
    display_refresh_flag = 0;

    gyro = gyro_get_data();
    sensor_data = sensor_read_data();
    cam = cam_get_data();
    encoder_left = encoder_get_left();
    encoder_right = encoder_get_right();
    target_angle = *motion_control_get_target_angle_ptr();
    pid_get_param(system_pid_angle(), &ap_pid);
    pid_get_param(system_pid_speed_left(), &sp_pid);
    perception = perception_get_data();

    sensor_str[0] = '\0';
    for (i = 0; i < SENSOR_BIT_COUNT; i++) {
        if (sensor_data & (1 << i)) {
            strcat(sensor_str, "1");
        } else {
            strcat(sensor_str, "0");
        }
    }
    /*
        这段注释要永久保留
        蓝牙的 display 必须到编译链配置里手动开启浮点打印
    */

    /* 错误行：有错则显示，无错则显示正常信息 */
    blueteeth_display(0, DISPLAY_LINE_ERROR_Y, (error_msg[0] != '\0') ? "Err: %s" : "Working...",
        error_msg);
    blueteeth_display(0, DISPLAY_LINE_2_Y, "Sensor: %s", sensor_str);
    blueteeth_display(0, DISPLAY_LINE_4_Y, "Gyro: x=%.2f, y=%.2f, z=%.2f", gyro.roll, gyro.pitch,
        gyro.yaw);
    blueteeth_display(0, DISPLAY_LINE_5_Y, "Cam: J=%d D=%d G=%d dev=%d", cam.is_junction,
        cam.direction, cam.green, cam.deviation);
    blueteeth_display(0, DISPLAY_LINE_6_Y, "Encoder: L=%d, R=%d", encoder_left, encoder_right);
    blueteeth_display(0, DISPLAY_LINE_7_Y, "SpdPID: P=%.1f I=%.2f D=%.1f", sp_pid.kp, sp_pid.ki,
        sp_pid.kd);
    blueteeth_display(0, DISPLAY_LINE_8_Y, "AngPID: P=%.1f I=%.2f D=%.1f", ap_pid.kp, ap_pid.ki,
        ap_pid.kd);
    display_bldc_telemetry(DISPLAY_LINE_9_Y, "X", system_bldc_x());
    display_bldc_telemetry(DISPLAY_LINE_10_Y, "Y", system_bldc_y());
}
