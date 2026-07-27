#ifndef AUTO_BALL_CAR_USER_SERVICES_DISPLAY_H
#define AUTO_BALL_CAR_USER_SERVICES_DISPLAY_H /* 头文件保护 */

#include "status.h"
#include <stdbool.h>
#include <stdint.h>

/* 蓝牙小程序显示区域的固定行坐标 */
typedef enum {
    DISPLAY_LINE_STATUS_Y = 0, /* 运行状态和心跳行 */
    DISPLAY_LINE_GYRO_Y = 40, /* 姿态角数据行 */
    DISPLAY_LINE_ENCODER_Y = 60, /* 双编码器周期增量行 */
    DISPLAY_LINE_SPEED_Y = 80, /* 速度闭环目标和 PWM 输出行 */
    DISPLAY_LINE_COMMAND_Y = 100, /* 蓝牙命令计数行 */
    DISPLAY_LINE_KEYS_Y = 120, /* 按键稳定状态和按下事件行 */
    DISPLAY_LINE_ULTRASONIC_Y = 140, /* 超声波距离和有效状态行 */
    DISPLAY_LINE_CAMERA_Y = 160, /* 相机目标、应答和序号行 */
} display_line_y_t;

/* 集中式诊断显示生命周期和状态缓存接口 */
void display_init(void);
status_code_t display_process(void);
status_code_t display_set_status(const char *message);
void display_set_command_count(uint16_t count);
void display_update_keys(uint8_t state, uint8_t pressed_events);
void display_set_speed_control(float target, float left_output, float right_output,
    bool is_active);

#endif /* AUTO_BALL_CAR_USER_SERVICES_DISPLAY_H */
