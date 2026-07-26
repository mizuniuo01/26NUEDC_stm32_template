/**
 * @file bsp_board.c
 * @brief Board composition root and semantic hardware capabilities.
 */
#include "bsp_board.h"
#include "bsp_board_config.h"

#include "dma.h"
#include "gpio.h"
#include "i2c.h"
#include "iwdg.h"
#include "tim.h"
#include "usart.h"

#include "command_service.h"
#include "driver_encoder.h"
#include "driver_camera_protocol.h"
#include "driver_gpio_output.h"
#include "driver_gyro_protocol.h"
#include "driver_keys.h"
#include "driver_motor.h"
#include "driver_oled.h"
#include "driver_sensor_mcu.h"
#include "driver_servo.h"
#include "driver_stepper.h"
#include "driver_ultrasonic.h"
#include "driver_uart_stream.h"
#include "error_service.h"

static driver_motor_t motor;
static driver_encoder_t left_encoder;
static driver_encoder_t right_encoder;
static driver_sensor_mcu_t line_sensor;
static driver_oled_t oled;
static driver_keys_t keys;
static driver_ultrasonic_t ultrasonic;
static driver_servo_t servo;
static driver_stepper_t stepper[2];
static driver_gpio_output_bank_t leds;
static driver_gpio_output_bank_t buzzer;
static driver_uart_stream_t bluetooth_stream;
static driver_uart_stream_t camera_stream;
static driver_uart_stream_t gyro_stream;
static driver_camera_protocol_t camera_protocol;
static driver_gyro_protocol_t gyro_protocol;
static command_service_t bluetooth_commands;
static uint8_t bluetooth_dispatch_buffer[DRIVER_UART_STREAM_BUFFER_SIZE];
static uint8_t camera_dispatch_buffer[DRIVER_UART_STREAM_BUFFER_SIZE];
static uint8_t gyro_dispatch_buffer[DRIVER_UART_STREAM_BUFFER_SIZE];
static uint8_t camera_tx_buffer[32];
static uint8_t board_initialized;
static uint8_t optional_unavailable;
static volatile uint32_t elapsed_ms;
static uint32_t next_encoder_ms;
static uint32_t next_sensor_ms;
static volatile uint32_t pending_error_sources;

static void record(status_source_t source, status_code_t code)
{
    if (code != STATUS_OK) {
        error_service_record(source, code, HAL_GetTick());
    }
}

status_code_t bsp_board_init(void)
{
    status_code_t status;
    driver_motor_config_t motor_config = {
        .timer = &htim3,
        .left_sleep_port = sleepl_GPIO_Port, .right_sleep_port = sleepr_GPIO_Port,
        .left_direction_port = dirl_GPIO_Port, .right_direction_port = dirr_GPIO_Port,
        .left_sleep_pin = sleepl_Pin, .right_sleep_pin = sleepr_Pin,
        .left_direction_pin = dirl_Pin, .right_direction_pin = dirr_Pin,
        .left_channel = TIM_CHANNEL_1, .right_channel = TIM_CHANNEL_2,
        .max_compare = BSP_MOTOR_PWM_PERIOD, .minimum_effective_compare = BSP_MOTOR_PWM_DEADZONE
    };
    driver_key_pin_t key_pins[DRIVER_KEYS_COUNT] = {
        {key1_GPIO_Port, key1_Pin, GPIO_PIN_RESET},
        {key2_GPIO_Port, key2_Pin, GPIO_PIN_RESET},
        {key3_GPIO_Port, key3_Pin, GPIO_PIN_RESET},
        {key4_GPIO_Port, key4_Pin, GPIO_PIN_RESET},
        {key5_GPIO_Port, key5_Pin, GPIO_PIN_RESET}
    };
    driver_gpio_output_pin_t led_pins[4] = {
        {led1_GPIO_Port, led1_Pin, GPIO_PIN_SET},
        {led2_GPIO_Port, led2_Pin, GPIO_PIN_SET},
        {led3_GPIO_Port, led3_Pin, GPIO_PIN_SET},
        {led4_GPIO_Port, led4_Pin, GPIO_PIN_SET}
    };
    driver_gpio_output_pin_t buzzer_pin = {buzzer_GPIO_Port, buzzer_Pin, GPIO_PIN_SET};

    error_service_init();
    command_service_init(&bluetooth_commands);
    driver_camera_protocol_init(&camera_protocol);
    driver_gyro_protocol_init(&gyro_protocol);

    status = driver_motor_init(&motor, &motor_config);
    record(STATUS_SOURCE_MOTOR, status);
    if (status != STATUS_OK) {
        return status;
    }
    status = driver_encoder_init(&left_encoder,
                                 &(driver_encoder_config_t){&htim2, -1, 32U});
    record(STATUS_SOURCE_ENCODER, status);
    if (status != STATUS_OK) {
        return status;
    }
    status = driver_encoder_init(&right_encoder,
                                 &(driver_encoder_config_t){&htim1, 1, 16U});
    record(STATUS_SOURCE_ENCODER, status);
    if (status != STATUS_OK) {
        return status;
    }
    status = driver_sensor_mcu_init(&line_sensor,
                                    &(driver_sensor_mcu_config_t){&hi2c2,
                                                                  BSP_SENSOR_I2C_ADDRESS_7BIT << 1U,
                                                                  BSP_SENSOR_READ_COMMAND});
    record(STATUS_SOURCE_SENSOR, status);
    if (status != STATUS_OK) {
        return status;
    }

    status = driver_gpio_output_init(&leds, led_pins, 4U);
    record(STATUS_SOURCE_BOARD, status);
    status = driver_gpio_output_init(&buzzer, &buzzer_pin, 1U);
    record(STATUS_SOURCE_BOARD, status);

    status = driver_oled_init(&oled,
                              &(driver_oled_config_t){&hi2c3, BSP_OLED_I2C_ADDRESS_HAL});
    if (status != STATUS_OK) {
        optional_unavailable |= 1U << 0U;
        record(STATUS_SOURCE_OLED, status);
    }
    status = driver_keys_init(&keys, key_pins, BSP_KEY_DEBOUNCE_MS);
    if (status != STATUS_OK) {
        optional_unavailable |= 1U << 1U;
        record(STATUS_SOURCE_KEY, status);
    }
    status = driver_ultrasonic_init(&ultrasonic,
                                    &(driver_ultrasonic_config_t){
                                        &htim4, TIM_CHANNEL_4, ultratrig_GPIO_Port,
                                        ultratrig_Pin, BSP_ULTRASONIC_PERIOD_MS});
    if (status != STATUS_OK) {
        optional_unavailable |= 1U << 2U;
        record(STATUS_SOURCE_ULTRASONIC, status);
    }
    status = driver_servo_init(&servo,
                               &(driver_servo_config_t){&huart4,
                                                        {BSP_SERVO_ID_BASE, BSP_SERVO_ID_TURRET,
                                                         BSP_SERVO_ID_AUX}, 3U,
                                                        BSP_SERVO_INTERVAL_MS, BSP_SERVO_POWER});
    if (status != STATUS_OK) {
        optional_unavailable |= 1U << 3U;
        record(STATUS_SOURCE_SERVO, status);
    }
    status = driver_stepper_init(&stepper[0],
                                 &(driver_stepper_config_t){&huart2, BSP_STEPPER_ID_LEFT});
    if (status != STATUS_OK) {
        optional_unavailable |= 1U << 4U;
        record(STATUS_SOURCE_STEPPER, status);
    }
    status = driver_stepper_init(&stepper[1],
                                 &(driver_stepper_config_t){&huart2, BSP_STEPPER_ID_RIGHT});
    if (status != STATUS_OK) {
        optional_unavailable |= 1U << 4U;
        record(STATUS_SOURCE_STEPPER, status);
    }

    status = driver_uart_stream_init(&bluetooth_stream, &huart1);
    if (status != STATUS_OK) {
        optional_unavailable |= 1U << 5U;
        record(STATUS_SOURCE_BLUETOOTH, status);
    }
    status = driver_uart_stream_init(&camera_stream, &huart3);
    if (status != STATUS_OK) {
        optional_unavailable |= 1U << 6U;
        record(STATUS_SOURCE_CAMERA, status);
    }
    status = driver_uart_stream_init(&gyro_stream, &huart6);
    if (status != STATUS_OK) {
        optional_unavailable |= 1U << 7U;
        record(STATUS_SOURCE_GYRO, status);
    }
    next_encoder_ms = HAL_GetTick() + BSP_ENCODER_PERIOD_MS;
    next_sensor_ms = HAL_GetTick() + BSP_SENSOR_PERIOD_MS;
    board_initialized = 1U;
    return STATUS_OK;
}

void bsp_board_process(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t pending_errors;
    uint16_t packet_length;
    status_code_t status;
    uint8_t source;

    if (board_initialized == 0U) {
        return;
    }
    __disable_irq();
    pending_errors = pending_error_sources;
    pending_error_sources = 0U;
    __enable_irq();
    for (source = 0U; source < STATUS_SOURCE_COUNT; source++) {
        if ((pending_errors & (1UL << source)) != 0U) {
            record((status_source_t)source, STATUS_IO_ERROR);
        }
    }
    (void)elapsed_ms;
    if ((int32_t)(now - next_encoder_ms) >= 0) {
        next_encoder_ms = now + BSP_ENCODER_PERIOD_MS;
        status = driver_encoder_process(&left_encoder);
        if (status != STATUS_OK) {
            record(STATUS_SOURCE_ENCODER, status);
        }
        status = driver_encoder_process(&right_encoder);
        if (status != STATUS_OK) {
            record(STATUS_SOURCE_ENCODER, status);
        }
    }
    if ((int32_t)(now - next_sensor_ms) >= 0) {
        next_sensor_ms = now + BSP_SENSOR_PERIOD_MS;
        status = driver_sensor_mcu_request(&line_sensor);
        if ((status != STATUS_OK) && (status != STATUS_BUSY)) {
            record(STATUS_SOURCE_SENSOR, status);
        }
    }
    status = driver_keys_process(&keys, now);
    if ((status != STATUS_OK) && (status != STATUS_NOT_INITIALIZED)) {
        record(STATUS_SOURCE_KEY, status);
    }
    status = driver_ultrasonic_process(&ultrasonic, now);
    if ((status != STATUS_OK) && (status != STATUS_NOT_INITIALIZED)) {
        record(STATUS_SOURCE_ULTRASONIC, status);
    }
    if (driver_uart_stream_take(&bluetooth_stream, bluetooth_dispatch_buffer,
                                sizeof(bluetooth_dispatch_buffer), &packet_length,
                                NULL) == STATUS_OK) {
        command_service_push(&bluetooth_commands, bluetooth_dispatch_buffer, packet_length);
    }
    if (driver_uart_stream_take(&camera_stream, camera_dispatch_buffer,
                                sizeof(camera_dispatch_buffer), &packet_length,
                                NULL) == STATUS_OK) {
        driver_camera_protocol_push(&camera_protocol, camera_dispatch_buffer, packet_length);
    }
    if (driver_uart_stream_take(&gyro_stream, gyro_dispatch_buffer,
                                sizeof(gyro_dispatch_buffer), &packet_length, NULL) == STATUS_OK) {
        driver_gyro_protocol_push(&gyro_protocol, gyro_dispatch_buffer, packet_length);
    }
    status = driver_oled_process(&oled);
    if ((status != STATUS_OK) && (status != STATUS_BUSY) &&
        (status != STATUS_NOT_INITIALIZED)) {
        record(STATUS_SOURCE_OLED, status);
    }
    /* Watchdog ownership stays here and is conditional on core initialization. */
    if ((motor.initialized != 0U) && (left_encoder.initialized != 0U) &&
        (right_encoder.initialized != 0U) && (line_sensor.initialized != 0U) &&
        (line_sensor.valid != 0U) &&
        ((uint32_t)(now - line_sensor.timestamp_ms) <= BSP_SENSOR_STALE_MS)) {
        if (HAL_IWDG_Refresh(&hiwdg) != HAL_OK) {
            record(STATUS_SOURCE_BOARD, STATUS_IO_ERROR);
        }
    }
}

void bsp_board_timer_elapsed_isr(void)
{
    elapsed_ms++;
}

void bsp_board_uart_rx_event_isr(void *uart_handle, uint16_t size)
{
    UART_HandleTypeDef *uart = (UART_HandleTypeDef *)uart_handle;

    if ((uart == NULL) || (size == 0U)) {
        return;
    }
    if (uart->Instance == USART1) {
        driver_uart_stream_rx_event_isr(&bluetooth_stream, size);
    } else if (uart->Instance == USART3) {
        driver_uart_stream_rx_event_isr(&camera_stream, size);
    } else if (uart->Instance == USART6) {
        driver_uart_stream_rx_event_isr(&gyro_stream, size);
    }
}

void bsp_board_uart_tx_complete_isr(void *uart_handle)
{
    UART_HandleTypeDef *uart = (UART_HandleTypeDef *)uart_handle;
    if (uart == NULL) {
        return;
    }
    driver_servo_tx_complete_isr(&servo, uart);
    driver_stepper_tx_complete_isr(&stepper[0], uart);
    driver_stepper_tx_complete_isr(&stepper[1], uart);
    driver_uart_stream_tx_complete_isr(&bluetooth_stream, uart);
    driver_uart_stream_tx_complete_isr(&camera_stream, uart);
    driver_uart_stream_tx_complete_isr(&gyro_stream, uart);
}

void bsp_board_uart_error_isr(void *uart_handle)
{
    UART_HandleTypeDef *uart = (UART_HandleTypeDef *)uart_handle;
    if (uart == NULL) {
        return;
    }
    driver_servo_error_isr(&servo, uart);
    driver_uart_stream_error_isr(&bluetooth_stream, uart);
    driver_uart_stream_error_isr(&camera_stream, uart);
    driver_uart_stream_error_isr(&gyro_stream, uart);
    if (uart->Instance == USART1) {
        pending_error_sources |= 1UL << STATUS_SOURCE_BLUETOOTH;
    } else if (uart->Instance == USART3) {
        pending_error_sources |= 1UL << STATUS_SOURCE_CAMERA;
    } else if (uart->Instance == USART6) {
        pending_error_sources |= 1UL << STATUS_SOURCE_GYRO;
    }
}

void bsp_board_i2c_rx_complete_isr(void *i2c_handle)
{
    I2C_HandleTypeDef *i2c = (I2C_HandleTypeDef *)i2c_handle;
    driver_sensor_mcu_rx_complete_isr(&line_sensor, i2c, HAL_GetTick());
}

void bsp_board_i2c_tx_complete_isr(void *i2c_handle)
{
    I2C_HandleTypeDef *i2c = (I2C_HandleTypeDef *)i2c_handle;
    driver_oled_tx_complete_isr(&oled, i2c);
}

void bsp_board_i2c_error_isr(void *i2c_handle)
{
    I2C_HandleTypeDef *i2c = (I2C_HandleTypeDef *)i2c_handle;
    driver_sensor_mcu_error_isr(&line_sensor, i2c);
    driver_oled_error_isr(&oled, i2c);
    if ((i2c != NULL) && (i2c->Instance == I2C2)) {
        pending_error_sources |= 1UL << STATUS_SOURCE_SENSOR;
    } else if ((i2c != NULL) && (i2c->Instance == I2C3)) {
        pending_error_sources |= 1UL << STATUS_SOURCE_OLED;
    }
}

void bsp_board_timer_capture_isr(void *timer_handle, uint32_t channel)
{
    driver_ultrasonic_capture_isr(&ultrasonic, (TIM_HandleTypeDef *)timer_handle, channel);
}

status_code_t bsp_drive_enable(void) { return driver_motor_enable(&motor); }
status_code_t bsp_drive_disable(void) { return driver_motor_disable(&motor); }
status_code_t bsp_drive_set(int16_t left, int16_t right)
{
    return driver_motor_set(&motor, left, right);
}

status_code_t bsp_feedback_get(bsp_feedback_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }
    snapshot->left_delta = driver_encoder_delta(&left_encoder);
    snapshot->right_delta = driver_encoder_delta(&right_encoder);
    snapshot->timestamp_ms = HAL_GetTick();
    return STATUS_OK;
}

status_code_t bsp_line_sensor_request(void) { return driver_sensor_mcu_request(&line_sensor); }

status_code_t bsp_line_sensor_get(bsp_sensor_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }
    return driver_sensor_mcu_snapshot(&line_sensor, &snapshot->value, &snapshot->valid,
                                      &snapshot->sequence, &snapshot->timestamp_ms);
}

status_code_t bsp_oled_clear(void)
{
    if (oled.initialized == 0U) {
        return STATUS_UNAVAILABLE;
    }
    driver_oled_clear(&oled);
    return STATUS_OK;
}

status_code_t bsp_oled_set_pixel(uint8_t x, uint8_t y, uint8_t on)
{
    return driver_oled_set_pixel(&oled, x, y, on);
}

status_code_t bsp_oled_refresh(void) { return driver_oled_refresh(&oled); }
status_code_t bsp_oled_process(void) { return driver_oled_process(&oled); }

status_code_t bsp_led_set(uint8_t mask)
{
    return driver_gpio_output_set_mask(&leds, mask);
}

status_code_t bsp_buzzer_set(uint8_t active)
{
    return driver_gpio_output_set(&buzzer, 0U, active);
}

status_code_t bsp_keys_get(uint8_t *state, uint8_t *pressed_events)
{
    if ((state == NULL) || (pressed_events == NULL)) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (keys.initialized == 0U) {
        return STATUS_UNAVAILABLE;
    }
    *state = driver_keys_state(&keys);
    *pressed_events = driver_keys_take_pressed(&keys);
    return STATUS_OK;
}

status_code_t bsp_ultrasonic_get(uint16_t *distance_mm, uint8_t *valid)
{
    return driver_ultrasonic_read(&ultrasonic, distance_mm, valid);
}

status_code_t bsp_servo_set_angle(uint8_t id, float angle)
{
    return driver_servo_set_angle(&servo, id, angle);
}

status_code_t bsp_stepper_enable(uint8_t id, uint8_t enable)
{
    if (id < 1U || id > 2U) {
        return STATUS_OUT_OF_RANGE;
    }
    return driver_stepper_enable(&stepper[id - 1U], enable);
}

status_code_t bsp_stepper_move(uint8_t id, int32_t pulses, uint16_t speed, uint8_t absolute)
{
    if (id < 1U || id > 2U) {
        return STATUS_OUT_OF_RANGE;
    }
    return driver_stepper_move(&stepper[id - 1U], pulses, speed, absolute);
}

status_code_t bsp_bluetooth_bind(const char *name, bsp_command_callback_t callback, void *context)
{
    return command_service_bind(&bluetooth_commands, name, callback, context) == 0 ?
           STATUS_OK : STATUS_OUT_OF_RANGE;
}

status_code_t bsp_bluetooth_write(const uint8_t *data, uint16_t length)
{
    return driver_uart_stream_write(&bluetooth_stream, data, length);
}

status_code_t bsp_camera_snapshot(bsp_camera_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }
    driver_camera_target_t target;
    status_code_t status = driver_camera_protocol_snapshot(&camera_protocol, &target);
    snapshot->error_x = target.error_x;
    snapshot->error_y = target.error_y;
    snapshot->has_target = target.has_target;
    snapshot->switch_ack = target.switch_ack;
    snapshot->switch_ack_id = target.switch_ack_id;
    snapshot->valid = target.valid;
    snapshot->sequence = target.sequence;
    return status;
}

status_code_t bsp_camera_switch(uint8_t enabled, uint8_t request_id)
{
    uint16_t length;
    status_code_t status = driver_camera_protocol_encode_switch(enabled, request_id,
                                                                 camera_tx_buffer,
                                                                 sizeof(camera_tx_buffer),
                                                                 &length);
    if (status != STATUS_OK) {
        return status;
    }
    return driver_uart_stream_write(&camera_stream, camera_tx_buffer, length);
}

status_code_t bsp_gyro_snapshot(bsp_gyro_snapshot_t *snapshot)
{
    driver_gyro_attitude_t attitude;

    if (snapshot == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }
    status_code_t status = driver_gyro_protocol_snapshot(&gyro_protocol, &attitude);
    snapshot->roll = attitude.roll;
    snapshot->pitch = attitude.pitch;
    snapshot->yaw = attitude.yaw;
    snapshot->valid = attitude.valid;
    snapshot->sequence = attitude.sequence;
    return status;
}

status_code_t bsp_board_health(bsp_board_health_t *health)
{
    if (health == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }
    health->initialized = board_initialized;
    health->motor_enabled = motor.enabled;
    health->sensor_valid = line_sensor.valid;
    health->optional_unavailable = optional_unavailable;
    health->timestamp_ms = HAL_GetTick();
    return board_initialized != 0U ? STATUS_OK : STATUS_NOT_INITIALIZED;
}
