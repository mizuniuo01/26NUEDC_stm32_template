/**
 * @file bsp_board.h
 * @brief Board composition root and semantic hardware capabilities.
 */
#ifndef USER_BSP_BSP_BOARD_H
#define USER_BSP_BSP_BOARD_H

#include <stdint.h>

#include "status.h"

typedef struct {
    uint8_t value;
    uint8_t valid;
    uint32_t sequence;
    uint32_t timestamp_ms;
} bsp_sensor_snapshot_t;

typedef struct {
    int32_t left_delta;
    int32_t right_delta;
    uint32_t timestamp_ms;
} bsp_feedback_snapshot_t;

typedef struct {
    uint8_t initialized;
    uint8_t motor_enabled;
    uint8_t sensor_valid;
    uint8_t optional_unavailable;
    uint32_t timestamp_ms;
} bsp_board_health_t;

typedef struct {
    int16_t error_x;
    int16_t error_y;
    uint8_t has_target;
    uint8_t switch_ack;
    uint8_t switch_ack_id;
    uint8_t valid;
    uint32_t sequence;
} bsp_camera_snapshot_t;

typedef struct {
    float roll;
    float pitch;
    float yaw;
    uint8_t valid;
    uint32_t sequence;
} bsp_gyro_snapshot_t;

typedef void (*bsp_command_callback_t)(const char *name, const char *value, void *context);

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

status_code_t bsp_drive_enable(void);
status_code_t bsp_drive_disable(void);
status_code_t bsp_drive_set(int16_t left, int16_t right);
status_code_t bsp_feedback_get(bsp_feedback_snapshot_t *snapshot);
status_code_t bsp_line_sensor_request(void);
status_code_t bsp_line_sensor_get(bsp_sensor_snapshot_t *snapshot);
status_code_t bsp_oled_clear(void);
status_code_t bsp_oled_set_pixel(uint8_t x, uint8_t y, uint8_t on);
status_code_t bsp_oled_refresh(void);
status_code_t bsp_oled_process(void);
status_code_t bsp_led_set(uint8_t mask);
status_code_t bsp_buzzer_set(uint8_t active);
status_code_t bsp_keys_get(uint8_t *state, uint8_t *pressed_events);
status_code_t bsp_ultrasonic_get(uint16_t *distance_mm, uint8_t *valid);
status_code_t bsp_servo_set_angle(uint8_t id, float angle);
status_code_t bsp_stepper_enable(uint8_t id, uint8_t enable);
status_code_t bsp_stepper_move(uint8_t id, int32_t pulses, uint16_t speed, uint8_t absolute);
status_code_t bsp_bluetooth_bind(const char *name, bsp_command_callback_t callback, void *context);
status_code_t bsp_bluetooth_write(const uint8_t *data, uint16_t length);
status_code_t bsp_camera_snapshot(bsp_camera_snapshot_t *snapshot);
status_code_t bsp_camera_switch(uint8_t enabled, uint8_t request_id);
status_code_t bsp_gyro_snapshot(bsp_gyro_snapshot_t *snapshot);
status_code_t bsp_board_health(bsp_board_health_t *health);

#endif
