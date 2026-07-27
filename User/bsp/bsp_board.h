#ifndef AUTO_BALL_CAR_USER_BSP_BSP_BOARD_H
#define AUTO_BALL_CAR_USER_BSP_BSP_BOARD_H /* 头文件保护 */

#include "status.h"
#include <stdbool.h>
#include <stdint.h>

/* 巡线传感器数据快照 */
typedef struct {
    uint8_t value;          /* 巡线传感器位掩码 */
    bool is_valid;          /* 数据有效标志 */
    uint32_t sequence;      /* 数据更新序号 */
    uint32_t timestamp_ms;  /* 最近更新时间，单位：毫秒 */
} bsp_sensor_snapshot_t;

/* 双轮编码器反馈快照 */
typedef struct {
    int32_t left_delta;     /* 左轮本周期编码器增量 */
    int32_t right_delta;    /* 右轮本周期编码器增量 */
    uint32_t sequence;      /* 10 ms 双编码器采样序号 */
    uint32_t timestamp_ms;  /* 采样时间，单位：毫秒 */
} bsp_feedback_snapshot_t;

/* 板级能力健康状态 */
typedef struct {
    bool is_initialized;                /* 板级组合根初始化完成标志 */
    bool is_motor_enabled;              /* 电机输出使能标志 */
    bool is_sensor_valid;               /* 巡线传感器数据有效标志 */
    uint8_t optional_unavailable_mask;  /* 不可用的可选设备位掩码 */
    uint32_t timestamp_ms;         /* 健康状态采集时间，单位：毫秒 */
} bsp_board_health_t;

/* 视觉目标数据快照 */
typedef struct {
    int16_t error_x;       /* 目标相对画面中心的横向像素偏差 */
    int16_t error_y;       /* 目标相对画面中心的纵向像素偏差 */
    bool has_target;         /* 检测到目标的标志 */
    bool has_switch_ack;     /* 视觉开关命令已应答标志 */
    uint8_t switch_ack_id;   /* 最近应答的视觉开关请求 ID */
    bool is_valid;           /* 快照有效标志 */
    uint32_t sequence;       /* 视觉数据更新序号 */
} bsp_camera_snapshot_t;

/* ZDT X42S 步进电机位置命令模式 */
typedef enum {
    BSP_STEPPER_MODE_RELATIVE_TARGET = 0x00U, /* 相对上一目标位置 */
    BSP_STEPPER_MODE_ABSOLUTE = 0x01U,        /* 相对坐标零点 */
    BSP_STEPPER_MODE_RELATIVE_CURRENT = 0x02U /* 相对当前实时位置 */
} bsp_stepper_move_mode_t;

/* 姿态角数据快照 */
typedef struct {
    float roll;        /* 横滚角，单位：度 */
    float pitch;       /* 俯仰角，单位：度 */
    float yaw;         /* 航向角，单位：度 */
    bool is_valid;     /* 快照有效标志 */
    uint32_t sequence; /* 姿态数据更新序号 */
} bsp_gyro_snapshot_t;

/* 蓝牙命令回调类型，名称和值仅在回调执行期间有效。 */
typedef void (*bsp_command_callback_t)(const char *name, const char *value, void *context);

/* 板级生命周期和中断转发接口 */
status_code_t bsp_board_init(void);
void bsp_board_process(void);
void bsp_board_timer_elapsed_isr(void);
void bsp_board_uart_rx_event_isr(void *uart_handle, uint16_t size);
void bsp_board_uart_tx_complete_isr(void *uart_handle);
void bsp_board_uart_error_isr(void *uart_handle);
void bsp_board_i2c_rx_complete_isr(void *i2c_handle);
void bsp_board_i2c_tx_complete_isr(void *i2c_handle);
void bsp_board_i2c_error_isr(void *i2c_handle);
void bsp_board_timer_capture_isr(void *timer_handle, uint32_t channel);

/* 底盘驱动与反馈接口 */
status_code_t bsp_drive_enable(void);
status_code_t bsp_drive_disable(void);
status_code_t bsp_drive_set(int16_t left, int16_t right);
status_code_t bsp_feedback_get(bsp_feedback_snapshot_t *snapshot);
status_code_t bsp_line_sensor_request(void);
status_code_t bsp_line_sensor_get(bsp_sensor_snapshot_t *snapshot);

/* 显示、指示器和输入接口 */
status_code_t bsp_oled_clear(void);
status_code_t bsp_oled_set_pixel(uint8_t x, uint8_t y, bool is_on);
status_code_t bsp_oled_refresh(void);
status_code_t bsp_oled_process(void);
status_code_t bsp_oled_frame_ready(bool *is_ready);
status_code_t bsp_led_set(uint8_t mask);
status_code_t bsp_buzzer_set(bool is_active);
status_code_t bsp_keys_get(uint8_t *state, uint8_t *pressed_events);
status_code_t bsp_ultrasonic_get(uint16_t *distance_mm, bool *is_valid);

/* 执行机构接口 */
status_code_t bsp_servo_set_angle(uint8_t id, float angle);
status_code_t bsp_stepper_enable(uint8_t id, bool is_enabled);
status_code_t bsp_stepper_move(uint8_t id, float angle, uint16_t speed, uint16_t acceleration,
    bsp_stepper_move_mode_t mode, bool is_synchronized);

/* 通信与感知接口 */
status_code_t bsp_bluetooth_bind(const char *name, bsp_command_callback_t callback, void *context);
status_code_t bsp_bluetooth_write(const uint8_t *data, uint16_t length);
status_code_t bsp_camera_snapshot(bsp_camera_snapshot_t *snapshot);
status_code_t bsp_camera_switch(bool is_enabled, uint8_t request_id);
status_code_t bsp_gyro_snapshot(bsp_gyro_snapshot_t *snapshot);

/* 板级状态查询接口 */
uint32_t bsp_time_get_ms(void);
status_code_t bsp_board_health(bsp_board_health_t *health);

#endif /* AUTO_BALL_CAR_USER_BSP_BSP_BOARD_H */
