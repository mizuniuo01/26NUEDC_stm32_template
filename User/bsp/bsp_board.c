/**
 * @file bsp_board.c
 * @brief 组合板级设备并向上层提供与具体外设无关的硬件能力。
 */
#include "bsp_board.h"
#include "bsp_board_config.h"
#include "command_service.h"
#include "driver_camera_protocol.h"
#include "driver_encoder.h"
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
#include <stddef.h>
#include "dma.h"
#include "gpio.h"
#include "i2c.h"
#include "iwdg.h"
#include "tim.h"
#include "usart.h"

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
static uint8_t bluetooth_dispatch_buffer[DRIVER_UART_STREAM_BUFFER_CAPACITY];
static uint8_t camera_dispatch_buffer[DRIVER_UART_STREAM_BUFFER_CAPACITY];
static uint8_t gyro_dispatch_buffer[DRIVER_UART_STREAM_BUFFER_CAPACITY];
static uint8_t camera_tx_buffer[32];
static bool is_board_initialized;
static uint8_t optional_unavailable_mask;
/* TIM6 ISR 累加、主循环读取；目标 MCU 已验证对齐 uint32_t 访问原子性。 */
static volatile uint32_t elapsed_ms;
#if BSP_LINE_SENSOR_ENABLED
static uint32_t next_sensor_ms;
#endif
static volatile uint32_t feedback_sequence;
static volatile uint32_t feedback_timestamp_ms;
static volatile uint8_t encoder_elapsed_ms;
static volatile bool is_encoder_sampling_enabled;
/* ISR 设置错误来源位，主循环在关中断临界区取走，避免读改写竞争。 */
static volatile uint32_t pending_error_sources;

_Static_assert(BSP_MOTOR_PWM_PERIOD_TICKS <= INT16_MAX,
    "motor PWM compare must fit in int16_t");
_Static_assert(BSP_MOTOR_PWM_DEAD_ZONE_TICKS <= BSP_MOTOR_PWM_PERIOD_TICKS,
    "motor PWM dead zone must not exceed the period");

/**
 * @brief  记录一次非成功状态
 * @param  source 状态来源模块
 * @param  code 需要记录的状态码，成功状态会被忽略
 */
static void record(status_source_t source, status_code_t code)
{
    if (code != STATUS_OK) {
        error_service_record(source, code, HAL_GetTick());
    }
}

/** @brief 取出一段蓝牙接收数据并推进命令解析。 */
static void process_bluetooth(void)
{
    uint16_t packet_length;

    if (driver_uart_stream_take(&bluetooth_stream, bluetooth_dispatch_buffer,
            sizeof(bluetooth_dispatch_buffer), &packet_length, NULL) == STATUS_OK) {
        command_service_push(&bluetooth_commands, bluetooth_dispatch_buffer, packet_length);
    }
}

/** @brief 在主循环正常推进时刷新看门狗。 */
static void refresh_watchdog(void)
{
    if (HAL_IWDG_Refresh(&hiwdg) != HAL_OK) {
        record(STATUS_SOURCE_BOARD, STATUS_IO_ERROR);
    }
}

/**
 * @brief  将千分比电机命令换算为当前 TIM3 周期的有符号比较值
 * @param  command 上层有符号命令，范围超出正负 1000 时先钳制
 * @return 适配 BSP_MOTOR_PWM_PERIOD_TICKS 的有符号比较值
 */
static int16_t scale_drive_command(int16_t command)
{
    int32_t limited_command = command;
    int32_t scaled_compare;

    if (limited_command > BSP_MOTOR_COMMAND_LIMIT) {
        limited_command = BSP_MOTOR_COMMAND_LIMIT;
    } else if (limited_command < -BSP_MOTOR_COMMAND_LIMIT) {
        limited_command = -BSP_MOTOR_COMMAND_LIMIT;
    }
    scaled_compare = limited_command * (int32_t)BSP_MOTOR_PWM_PERIOD_TICKS /
                     BSP_MOTOR_COMMAND_LIMIT;
    return (int16_t)scaled_compare;
}

/**
 * @brief  初始化板级必需设备和可选设备
 * @retval STATUS_OK 板级必需设备初始化成功
 * @retval STATUS_INVALID_ARGUMENT 必需设备配置不合法
 * @retval STATUS_IO_ERROR 必需外设启动失败
 * @retval STATUS_STATE_ERROR TIM3 预分频或自动重装值与 BSP 电机配置不一致
 */
status_code_t bsp_board_init(void)
{
    static const uint8_t servo_ids[] = {BSP_SERVO_ID_1};
    uint32_t interrupt_mask;
    status_code_t status;
    driver_motor_config_t motor_config = {
        .timer = &htim3,
        .left_sleep_port = sleepl_GPIO_Port,
        .right_sleep_port = sleepr_GPIO_Port,
        .left_direction_port = dirl_GPIO_Port,
        .right_direction_port = dirr_GPIO_Port,
        .left_sleep_pin = sleepl_Pin,
        .right_sleep_pin = sleepr_Pin,
        .left_direction_pin = dirl_Pin,
        .right_direction_pin = dirr_Pin,
        .left_channel = TIM_CHANNEL_1,
        .right_channel = TIM_CHANNEL_2,
        .max_compare = BSP_MOTOR_PWM_PERIOD_TICKS,
        .minimum_effective_compare = BSP_MOTOR_PWM_DEAD_ZONE_TICKS,
    };
    /* PCB 按键 1–4 与 CubeMX key1–key4 标签反序，BSP 在此统一物理编号。 */
    driver_key_pin_t key_pins[DRIVER_KEYS_COUNT] = {
        {
            .port = key4_GPIO_Port,
            .pin = key4_Pin,
            .active_level = GPIO_PIN_RESET,
        },
        {
            .port = key3_GPIO_Port,
            .pin = key3_Pin,
            .active_level = GPIO_PIN_RESET,
        },
        {
            .port = key2_GPIO_Port,
            .pin = key2_Pin,
            .active_level = GPIO_PIN_RESET,
        },
        {
            .port = key1_GPIO_Port,
            .pin = key1_Pin,
            .active_level = GPIO_PIN_RESET,
        },
        {
            .port = key5_GPIO_Port,
            .pin = key5_Pin,
            .active_level = GPIO_PIN_RESET,
        },
    };
    driver_gpio_output_pin_t led_pins[4] = {
        {
            .port = led1_GPIO_Port,
            .pin = led1_Pin,
            .active_level = GPIO_PIN_SET,
        },
        {
            .port = led2_GPIO_Port,
            .pin = led2_Pin,
            .active_level = GPIO_PIN_SET,
        },
        {
            .port = led3_GPIO_Port,
            .pin = led3_Pin,
            .active_level = GPIO_PIN_SET,
        },
        {
            .port = led4_GPIO_Port,
            .pin = led4_Pin,
            .active_level = GPIO_PIN_SET,
        },
    };
    driver_gpio_output_pin_t buzzer_pin = {
        .port = buzzer_GPIO_Port,
        .pin = buzzer_Pin,
        .active_level = GPIO_PIN_SET,
    };

    error_service_init();
    command_service_init(&bluetooth_commands);
    driver_camera_protocol_init(&camera_protocol);
    driver_gyro_protocol_init(&gyro_protocol);
    status = driver_uart_stream_init(&bluetooth_stream, &huart1);
    record(STATUS_SOURCE_BLUETOOTH, status);
    if (status != STATUS_OK) {
        optional_unavailable_mask |= 1U << 5U;
    }

#if BSP_LINE_SENSOR_ENABLED
    status = driver_sensor_mcu_init(&line_sensor,
        &(driver_sensor_mcu_config_t){
            .i2c = &hi2c2,
            .address = BSP_SENSOR_I2C_ADDRESS_7BIT << 1U,
            .command = BSP_SENSOR_READ_COMMAND,
        });
    record(STATUS_SOURCE_SENSOR, status);
    if (status != STATUS_OK) {
        return status;
    }
    next_sensor_ms = HAL_GetTick() + BSP_SENSOR_PERIOD_MS;
#endif

    status = driver_uart_stream_init(&gyro_stream, &huart6);
    record(STATUS_SOURCE_GYRO, status);
    if (status != STATUS_OK) {
        optional_unavailable_mask |= 1U << 7U;
    }

    if ((htim3.Init.Prescaler != BSP_MOTOR_PWM_PRESCALER) ||
        (__HAL_TIM_GET_AUTORELOAD(&htim3) != (BSP_MOTOR_PWM_PERIOD_TICKS - 1U))) {
        record(STATUS_SOURCE_MOTOR, STATUS_STATE_ERROR);
        return STATUS_STATE_ERROR;
    }
    status = driver_motor_init(&motor, &motor_config);
    record(STATUS_SOURCE_MOTOR, status);
    if (status != STATUS_OK) {
        return status;
    }
    status = driver_encoder_init(&left_encoder,
        &(driver_encoder_config_t){
            .timer = &htim2,
            .sign = -1,
            .counter_bits = 32U,
        });
    record(STATUS_SOURCE_ENCODER, status);
    if (status != STATUS_OK) {
        return status;
    }
    status = driver_encoder_init(&right_encoder,
        &(driver_encoder_config_t){
            .timer = &htim1,
            .sign = 1,
            .counter_bits = 16U,
        });
    record(STATUS_SOURCE_ENCODER, status);
    if (status != STATUS_OK) {
        return status;
    }
    interrupt_mask = __get_PRIMASK();
    __disable_irq();
    feedback_sequence = 0U;
    feedback_timestamp_ms = elapsed_ms;
    encoder_elapsed_ms = 0U;
    is_encoder_sampling_enabled = true;
    __set_PRIMASK(interrupt_mask);
    status = driver_gpio_output_init(&leds, led_pins, 4U);
    record(STATUS_SOURCE_BOARD, status);
    status = driver_gpio_output_init(&buzzer, &buzzer_pin, 1U);
    record(STATUS_SOURCE_BOARD, status);

    status = driver_oled_init(&oled,
        &(driver_oled_config_t){
            .i2c = &hi2c3,
            .address = BSP_OLED_I2C_ADDRESS_HAL,
        });
    if (status != STATUS_OK) {
        optional_unavailable_mask |= 1U << 0U;
        record(STATUS_SOURCE_OLED, status);
    }
    status = driver_keys_init(&keys, key_pins, BSP_KEY_DEBOUNCE_MS);
    if (status != STATUS_OK) {
        optional_unavailable_mask |= 1U << 1U;
        record(STATUS_SOURCE_KEY, status);
    }
    status = driver_ultrasonic_init(&ultrasonic,
        &(driver_ultrasonic_config_t){
            .timer = &htim4,
            .channel = TIM_CHANNEL_4,
            .trigger_port = ultratrig_GPIO_Port,
            .trigger_pin = ultratrig_Pin,
            .trigger_period_ms = BSP_ULTRASONIC_PERIOD_MS,
        });
    if (status != STATUS_OK) {
        optional_unavailable_mask |= 1U << 2U;
        record(STATUS_SOURCE_ULTRASONIC, status);
    }
    status = driver_servo_init(&servo,
        &(driver_servo_config_t){
            .uart = &huart4,
            .interval_ms = BSP_SERVO_INTERVAL_MS,
            .power = BSP_SERVO_POWER,
        },
        servo_ids, (uint8_t)(sizeof(servo_ids) / sizeof(servo_ids[0])));
    if (status != STATUS_OK) {
        optional_unavailable_mask |= 1U << 3U;
        record(STATUS_SOURCE_SERVO, status);
    }
    status = driver_stepper_init(&stepper[0],
        &(driver_stepper_config_t){
            .uart = &huart2,
            .id = BSP_STEPPER_ID_LEFT,
        });
    if (status != STATUS_OK) {
        optional_unavailable_mask |= 1U << 4U;
        record(STATUS_SOURCE_STEPPER, status);
    }
    status = driver_stepper_init(&stepper[1],
        &(driver_stepper_config_t){
            .uart = &huart2,
            .id = BSP_STEPPER_ID_RIGHT,
        });
    if (status != STATUS_OK) {
        optional_unavailable_mask |= 1U << 4U;
        record(STATUS_SOURCE_STEPPER, status);
    }

    status = driver_uart_stream_init(&camera_stream, &huart3);
    if (status != STATUS_OK) {
        optional_unavailable_mask |= 1U << 6U;
        record(STATUS_SOURCE_CAMERA, status);
    }
    is_board_initialized = true;
    return STATUS_OK;
}

/** @brief 推进板级周期任务、通信解析和看门狗刷新。 */
void bsp_board_process(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t pending_errors;
    uint16_t packet_length;
    status_code_t status;
    uint8_t source;

    if (!is_board_initialized) {
        return;
    }
    __disable_irq();
    pending_errors = pending_error_sources;
    pending_error_sources = 0U;
    __enable_irq();
    for (source = 0U; source < STATUS_SOURCE_COUNT; source++) {
        if ((pending_errors & (UINT32_C(1) << source)) != 0U) {
            record((status_source_t)source, STATUS_IO_ERROR);
        }
    }
    process_bluetooth();
#if BSP_LINE_SENSOR_ENABLED
    if ((int32_t)(now - next_sensor_ms) >= 0) {
        next_sensor_ms = now + BSP_SENSOR_PERIOD_MS;
        status = driver_sensor_mcu_request(&line_sensor);
        if ((status != STATUS_OK) && (status != STATUS_BUSY)) {
            record(STATUS_SOURCE_SENSOR, status);
        }
    }
#endif
    if (driver_uart_stream_take(&gyro_stream, gyro_dispatch_buffer,
            sizeof(gyro_dispatch_buffer), &packet_length, NULL) == STATUS_OK) {
        driver_gyro_protocol_push(&gyro_protocol, gyro_dispatch_buffer, packet_length);
    }
    status = driver_keys_process(&keys, now);
    if ((status != STATUS_OK) && (status != STATUS_NOT_INITIALIZED)) {
        record(STATUS_SOURCE_KEY, status);
    }
    status = driver_ultrasonic_process(&ultrasonic, now);
    if ((status != STATUS_OK) && (status != STATUS_NOT_INITIALIZED)) {
        record(STATUS_SOURCE_ULTRASONIC, status);
    }
    if (driver_uart_stream_take(&camera_stream, camera_dispatch_buffer,
            sizeof(camera_dispatch_buffer), &packet_length, NULL) == STATUS_OK) {
        driver_camera_protocol_push(&camera_protocol, camera_dispatch_buffer, packet_length);
    }
    status = driver_oled_process(&oled);
    if ((status != STATUS_OK) && (status != STATUS_BUSY) && (status != STATUS_NOT_INITIALIZED)) {
        record(STATUS_SOURCE_OLED, status);
    }
    /* 看门狗只监督主循环是否持续推，不把业务传感器作为喂狗前置。 */
    refresh_watchdog();
}

/** @brief 累加板级时间并在精确 10 ms 节点采样双编码器。 */
void bsp_board_timer_elapsed_isr(void)
{
    status_code_t left_status;
    status_code_t right_status;

    elapsed_ms += BSP_BOARD_TIMER_PERIOD_MS;
    if (!is_encoder_sampling_enabled) {
        return;
    }
    encoder_elapsed_ms = (uint8_t)(encoder_elapsed_ms + BSP_BOARD_TIMER_PERIOD_MS);
    if (encoder_elapsed_ms < BSP_ENCODER_PERIOD_MS) {
        return;
    }
    encoder_elapsed_ms = 0U;
    left_status = driver_encoder_process(&left_encoder);
    right_status = driver_encoder_process(&right_encoder);
    if ((left_status == STATUS_OK) && (right_status == STATUS_OK)) {
        feedback_sequence++;
        feedback_timestamp_ms = elapsed_ms;
    } else {
        pending_error_sources |= UINT32_C(1) << STATUS_SOURCE_ENCODER;
    }
}

/**
 * @brief  将串口接收到空闲事件路由到对应的数据流驱动
 * @param  uart_handle STM32 HAL 串口句柄
 * @param  size 当前 DMA 缓冲区收到的字节数
 */
void bsp_board_uart_rx_event_isr(void *uart_handle, uint16_t size)
{
    UART_HandleTypeDef *uart = (UART_HandleTypeDef *)uart_handle;

    if (!uart || (size == 0U)) {
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

/**
 * @brief  将串口发送完成事件转发到全部串口设备驱动
 * @param  uart_handle STM32 HAL 串口句柄
 */
void bsp_board_uart_tx_complete_isr(void *uart_handle)
{
    UART_HandleTypeDef *uart = (UART_HandleTypeDef *)uart_handle;
    if (!uart) {
        return;
    }
    driver_servo_tx_complete_isr(&servo, uart);
    driver_stepper_tx_complete_isr(&stepper[0], uart);
    driver_stepper_tx_complete_isr(&stepper[1], uart);
    driver_uart_stream_tx_complete_isr(&bluetooth_stream, uart);
    driver_uart_stream_tx_complete_isr(&camera_stream, uart);
    driver_uart_stream_tx_complete_isr(&gyro_stream, uart);
}

/**
 * @brief  将串口错误事件转发到设备驱动并标记诊断来源
 * @param  uart_handle STM32 HAL 串口句柄
 */
void bsp_board_uart_error_isr(void *uart_handle)
{
    UART_HandleTypeDef *uart = (UART_HandleTypeDef *)uart_handle;
    if (!uart) {
        return;
    }
    driver_servo_error_isr(&servo, uart);
    driver_stepper_error_isr(&stepper[0], uart);
    driver_stepper_error_isr(&stepper[1], uart);
    driver_uart_stream_error_isr(&bluetooth_stream, uart);
    driver_uart_stream_error_isr(&camera_stream, uart);
    driver_uart_stream_error_isr(&gyro_stream, uart);
    if (uart->Instance == USART1) {
        pending_error_sources |= UINT32_C(1) << STATUS_SOURCE_BLUETOOTH;
    } else if (uart->Instance == USART3) {
        pending_error_sources |= UINT32_C(1) << STATUS_SOURCE_CAMERA;
    } else if (uart->Instance == USART6) {
        pending_error_sources |= UINT32_C(1) << STATUS_SOURCE_GYRO;
    } else if (uart->Instance == USART2) {
        pending_error_sources |= UINT32_C(1) << STATUS_SOURCE_STEPPER;
    }
}

/**
 * @brief  将 I2C 存储器读取完成事件转发到巡线传感器驱动
 * @param  i2c_handle STM32 HAL I2C 句柄
 */
void bsp_board_i2c_rx_complete_isr(void *i2c_handle)
{
    I2C_HandleTypeDef *i2c = (I2C_HandleTypeDef *)i2c_handle;
    driver_sensor_mcu_rx_complete_isr(&line_sensor, i2c, HAL_GetTick());
}

/**
 * @brief  将 I2C 主机发送完成事件转发到 OLED 驱动
 * @param  i2c_handle STM32 HAL I2C 句柄
 */
void bsp_board_i2c_tx_complete_isr(void *i2c_handle)
{
    I2C_HandleTypeDef *i2c = (I2C_HandleTypeDef *)i2c_handle;
    driver_oled_tx_complete_isr(&oled, i2c);
}

/**
 * @brief  将 I2C 错误事件转发到设备驱动并标记诊断来源
 * @param  i2c_handle STM32 HAL I2C 句柄
 */
void bsp_board_i2c_error_isr(void *i2c_handle)
{
    I2C_HandleTypeDef *i2c = (I2C_HandleTypeDef *)i2c_handle;
    driver_sensor_mcu_error_isr(&line_sensor, i2c);
    driver_oled_error_isr(&oled, i2c);
    if (i2c && (i2c->Instance == I2C2)) {
        pending_error_sources |= UINT32_C(1) << STATUS_SOURCE_SENSOR;
    } else if (i2c && (i2c->Instance == I2C3)) {
        pending_error_sources |= UINT32_C(1) << STATUS_SOURCE_OLED;
    }
}

/**
 * @brief  将定时器输入捕获事件转发到超声波驱动
 * @param  timer_handle STM32 HAL 定时器句柄
 * @param  channel 发生捕获事件的 HAL 定时器通道
 */
void bsp_board_timer_capture_isr(void *timer_handle, uint32_t channel)
{
    driver_ultrasonic_capture_isr(&ultrasonic, (TIM_HandleTypeDef *)timer_handle, channel);
}

/**
 * @brief  使能双路底盘电机输出
 * @retval STATUS_OK 电机已使能
 * @retval STATUS_NOT_INITIALIZED 电机驱动尚未初始化
 */
status_code_t bsp_drive_enable(void)
{
    return driver_motor_enable(&motor);
}
/**
 * @brief  禁用双路底盘电机输出
 * @retval STATUS_OK 电机已进入禁用状态
 * @retval STATUS_NOT_INITIALIZED 电机驱动尚未初始化
 */
status_code_t bsp_drive_disable(void)
{
    return driver_motor_disable(&motor);
}
/**
 * @brief  设置左右底盘电机的有符号千分比目标
 * @param  left 左电机目标，正负 1000 对应正反向满占空比
 * @param  right 右电机目标，正负 1000 对应正反向满占空比
 * @retval STATUS_OK 输出已更新
 * @retval STATUS_NOT_INITIALIZED 电机驱动尚未初始化
 * @retval STATUS_STATE_ERROR 电机输出尚未使能
 */
status_code_t bsp_drive_set(int16_t left, int16_t right)
{
    return driver_motor_set(&motor, scale_drive_command(left), scale_drive_command(right));
}

/**
 * @brief  获取双轮编码器反馈快照
 * @param  snapshot 接收反馈数据的存储地址
 * @retval STATUS_OK 快照已写入
 * @retval STATUS_INVALID_ARGUMENT snapshot 为空
 * @retval STATUS_NOT_INITIALIZED 任一路编码器尚未初始化
 */
status_code_t bsp_feedback_get(bsp_feedback_snapshot_t *snapshot)
{
    uint32_t sequence_before;
    uint32_t sequence_after;

    if (!snapshot) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!left_encoder.is_initialized || !right_encoder.is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    do {
        sequence_before = feedback_sequence;
        snapshot->left_delta = driver_encoder_delta(&left_encoder);
        snapshot->right_delta = driver_encoder_delta(&right_encoder);
        snapshot->timestamp_ms = feedback_timestamp_ms;
        sequence_after = feedback_sequence;
    } while (sequence_before != sequence_after);
    snapshot->sequence = sequence_after;
    return STATUS_OK;
}

/**
 * @brief  请求一次非阻塞巡线传感器采集
 * @retval STATUS_OK I2C DMA 读取已启动
 * @retval STATUS_NOT_INITIALIZED 传感器驱动尚未初始化
 * @retval STATUS_BUSY 上一次 I2C DMA 事务尚未结束
 * @retval STATUS_IO_ERROR I2C DMA 读取启动失败
 */
status_code_t bsp_line_sensor_request(void)
{
    return driver_sensor_mcu_request(&line_sensor);
}

/**
 * @brief  获取最近一次巡线传感器快照
 * @param  snapshot 接收传感器数据的存储地址
 * @retval STATUS_OK 快照已写入
 * @retval STATUS_INVALID_ARGUMENT snapshot 为空
 * @retval STATUS_NOT_INITIALIZED 传感器驱动尚未初始化
 */
status_code_t bsp_line_sensor_get(bsp_sensor_snapshot_t *snapshot)
{
    if (!snapshot) {
        return STATUS_INVALID_ARGUMENT;
    }
    return driver_sensor_mcu_snapshot(&line_sensor, &snapshot->value, &snapshot->is_valid,
        &snapshot->sequence, &snapshot->timestamp_ms);
}

/**
 * @brief  清空 OLED 显存但不立即发起刷新
 * @retval STATUS_OK 显存已清空
 * @retval STATUS_UNAVAILABLE OLED 驱动不可用
 */
status_code_t bsp_oled_clear(void)
{
    if (!oled.is_initialized) {
        return STATUS_UNAVAILABLE;
    }
    driver_oled_clear(&oled);
    return STATUS_OK;
}

/**
 * @brief  设置 OLED 显存中的一个像素
 * @param  x 像素横坐标
 * @param  y 像素纵坐标
 * @param  is_on 像素点亮标志
 * @retval STATUS_OK 像素已更新
 * @retval STATUS_NOT_INITIALIZED OLED 驱动尚未初始化
 * @retval STATUS_OUT_OF_RANGE 像素坐标超出屏幕范围
 */
status_code_t bsp_oled_set_pixel(uint8_t x, uint8_t y, bool is_on)
{
    return driver_oled_set_pixel(&oled, x, y, is_on);
}

/**
 * @brief  请求一次完整 OLED 显存刷新
 * @retval STATUS_OK 刷新请求已登记
 * @retval STATUS_NOT_INITIALIZED OLED 驱动尚未初始化
 */
status_code_t bsp_oled_refresh(void)
{
    return driver_oled_refresh(&oled);
}
/**
 * @brief  推进 OLED 非阻塞刷新状态机
 * @retval STATUS_OK 当前无需发送或一页发送已启动
 * @retval STATUS_NOT_INITIALIZED OLED 驱动尚未初始化
 * @retval STATUS_BUSY I2C3 正忙
 * @retval STATUS_IO_ERROR OLED 命令或数据发送失败
 */
status_code_t bsp_oled_process(void)
{
    return driver_oled_process(&oled);
}

/**
 * @brief  查询 OLED 显存是否可由唯一上层所有者开始绘制新帧
 * @param  is_ready 接收显存可写且无待发送帧的标志
 * @retval STATUS_OK 就绪状态已写入
 * @retval STATUS_INVALID_ARGUMENT is_ready 为空
 * @retval STATUS_UNAVAILABLE OLED 驱动尚未初始化
 * @retval STATUS_IO_ERROR OLED 已进入异步传输故障状态
 */
status_code_t bsp_oled_frame_ready(bool *is_ready)
{
    if (!is_ready) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!oled.is_initialized) {
        return STATUS_UNAVAILABLE;
    }
    if (oled.has_fault) {
        *is_ready = false;
        return STATUS_IO_ERROR;
    }
    *is_ready = oled.is_ready && !oled.is_busy && !oled.is_refresh_requested &&
                (oled.transfer == DRIVER_OLED_TRANSFER_IDLE) && (oled.page == 0U);
    return STATUS_OK;
}

/**
 * @brief  按位掩码设置四个板载 LED
 * @param  mask 每一位非零表示对应 LED 点亮
 * @retval STATUS_OK LED 状态已更新
 * @retval STATUS_NOT_INITIALIZED GPIO 输出组尚未初始化
 */
status_code_t bsp_led_set(uint8_t mask)
{
    return driver_gpio_output_set_mask(&leds, mask);
}

/**
 * @brief  设置板载蜂鸣器状态
 * @param  is_active 蜂鸣器启用标志
 * @retval STATUS_OK 蜂鸣器状态已更新
 * @retval STATUS_NOT_INITIALIZED GPIO 输出组尚未初始化
 */
status_code_t bsp_buzzer_set(bool is_active)
{
    return driver_gpio_output_set(&buzzer, 0U, is_active);
}

/**
 * @brief  获取按键稳定状态并取走尚未处理的按下事件
 * @param  state 接收当前按键状态位掩码
 * @param  pressed_events 接收并清除按下事件位掩码
 * @retval STATUS_OK 按键数据已写入
 * @retval STATUS_INVALID_ARGUMENT 任一输出地址为空
 * @retval STATUS_UNAVAILABLE 按键驱动不可用
 */
status_code_t bsp_keys_get(uint8_t *state, uint8_t *pressed_events)
{
    if (!state || !pressed_events) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!keys.is_initialized) {
        return STATUS_UNAVAILABLE;
    }
    *state = driver_keys_state(&keys);
    *pressed_events = driver_keys_take_pressed(&keys);
    return STATUS_OK;
}

/**
 * @brief  获取最近一次超声波测距结果
 * @param  distance_mm 接收距离，单位：毫米
 * @param  is_valid 接收距离有效标志
 * @retval STATUS_OK 测距结果已写入
 * @retval STATUS_INVALID_ARGUMENT 任一输出地址为空
 * @retval STATUS_NOT_INITIALIZED 超声波驱动尚未初始化
 */
status_code_t bsp_ultrasonic_get(uint16_t *distance_mm, bool *is_valid)
{
    return driver_ultrasonic_read(&ultrasonic, distance_mm, is_valid);
}

/**
 * @brief  设置指定总线舵机的目标角度
 * @param  id 舵机总线 ID
 * @param  angle 目标角度，单位：度
 * @retval STATUS_OK 舵机命令发送已启动
 * @retval STATUS_NOT_INITIALIZED 舵机驱动尚未初始化
 * @retval STATUS_OUT_OF_RANGE ID 或角度超出支持范围
 * @retval STATUS_BUSY 串口 DMA 正忙
 * @retval STATUS_IO_ERROR 串口 DMA 发送启动失败
 */
status_code_t bsp_servo_set_angle(uint8_t id, float angle)
{
    return driver_servo_set_angle(&servo, id, angle);
}

/**
 * @brief  设置指定步进电机的使能状态
 * @param  id 步进电机板级 ID，取值为 1 或 2
 * @param  is_enabled 步进电机使能标志
 * @retval STATUS_OK 步进电机命令发送已启动
 * @retval STATUS_OUT_OF_RANGE id 超出支持范围
 * @retval STATUS_NOT_INITIALIZED 步进电机驱动尚未初始化
 * @retval STATUS_BUSY 串口 DMA 正忙
 * @retval STATUS_IO_ERROR 串口 DMA 发送启动失败
 */
status_code_t bsp_stepper_enable(uint8_t id, bool is_enabled)
{
    if (id < 1U || id > 2U) {
        return STATUS_OUT_OF_RANGE;
    }
    return driver_stepper_enable(&stepper[id - 1U], is_enabled);
}

/**
 * @brief  向指定步进电机发送位置命令
 * @param  id 步进电机板级 ID，取值为 1 或 2
 * @param  angle 目标角度，符号表示方向
 * @param  speed 协议速度参数
 * @param  acceleration 协议加速度参数
 * @param  mode 位置模式
 * @param  is_synchronized 等待多机同步触发标志
 * @retval STATUS_OK 步进电机命令发送已启动
 * @retval STATUS_OUT_OF_RANGE id 超出支持范围
 * @retval STATUS_NOT_INITIALIZED 步进电机驱动尚未初始化
 * @retval STATUS_BUSY 串口 DMA 正忙
 * @retval STATUS_IO_ERROR 串口 DMA 发送启动失败
 */
status_code_t bsp_stepper_move(uint8_t id, float angle, uint16_t speed, uint16_t acceleration,
    bsp_stepper_move_mode_t mode, bool is_synchronized)
{
    if (id < 1U || id > 2U) {
        return STATUS_OUT_OF_RANGE;
    }
    if ((mode != BSP_STEPPER_MODE_RELATIVE_TARGET) && (mode != BSP_STEPPER_MODE_ABSOLUTE) &&
        (mode != BSP_STEPPER_MODE_RELATIVE_CURRENT)) {
        return STATUS_OUT_OF_RANGE;
    }
    return driver_stepper_move(&stepper[id - 1U], angle, speed, acceleration,
        (driver_stepper_move_mode_t)mode, is_synchronized);
}

/**
 * @brief  将蓝牙文本命令名称绑定到处理回调
 * @param  name 以空字符结尾的命令名称
 * @param  callback 收到同名命令时调用的处理函数
 * @param  context 调用回调时透传的上下文
 * @retval STATUS_OK 回调绑定成功
 * @retval STATUS_OUT_OF_RANGE 参数非法、名称过长或绑定表已满
 */
status_code_t bsp_bluetooth_bind(const char *name, bsp_command_callback_t callback, void *context)
{
    return command_service_bind(&bluetooth_commands, name, callback, context) == STATUS_OK
               ? STATUS_OK
               : STATUS_OUT_OF_RANGE;
}

/**
 * @brief  通过蓝牙串口异步发送字节流
 * @param  data 待发送数据
 * @param  length 待发送字节数
 * @retval STATUS_OK DMA 发送已启动
 * @retval STATUS_INVALID_ARGUMENT 数据地址为空、长度为零或超过缓冲区容量
 * @retval STATUS_NOT_INITIALIZED 蓝牙串口流尚未初始化
 * @retval STATUS_BUSY 上一次 DMA 发送尚未结束
 * @retval STATUS_IO_ERROR DMA 发送启动失败
 */
status_code_t bsp_bluetooth_write(const uint8_t *data, uint16_t length)
{
    return driver_uart_stream_write(&bluetooth_stream, data, length);
}

/**
 * @brief  获取最近一次视觉目标快照
 * @param  snapshot 接收视觉目标数据的存储地址
 * @retval STATUS_OK 快照有效且已写入
 * @retval STATUS_INVALID_ARGUMENT snapshot 为空
 * @retval STATUS_UNAVAILABLE 尚未收到有效视觉数据
 */
status_code_t bsp_camera_snapshot(bsp_camera_snapshot_t *snapshot)
{
    if (!snapshot) {
        return STATUS_INVALID_ARGUMENT;
    }
    driver_camera_target_t target;
    status_code_t status = driver_camera_protocol_snapshot(&camera_protocol, &target);
    snapshot->error_x = target.error_x;
    snapshot->error_y = target.error_y;
    snapshot->has_target = target.has_target;
    snapshot->has_switch_ack = target.has_switch_ack;
    snapshot->switch_ack_id = target.switch_ack_id;
    snapshot->is_valid = target.is_valid;
    snapshot->sequence = target.sequence;
    return status;
}

/**
 * @brief  异步发送视觉处理开关命令
 * @param  is_enabled 视觉处理启用标志
 * @param  request_id 用于匹配应答的请求 ID
 * @retval STATUS_OK DMA 发送已启动
 * @retval STATUS_INVALID_ARGUMENT 编码输出缓冲区不满足协议要求
 * @retval STATUS_NOT_INITIALIZED 视觉串口流尚未初始化
 * @retval STATUS_BUSY 上一次 DMA 发送尚未结束
 * @retval STATUS_IO_ERROR DMA 发送启动失败
 */
status_code_t bsp_camera_switch(bool is_enabled, uint8_t request_id)
{
    uint16_t length;
    status_code_t status = driver_camera_protocol_encode_switch(is_enabled, request_id,
        camera_tx_buffer, sizeof(camera_tx_buffer), &length);
    if (status != STATUS_OK) {
        return status;
    }
    return driver_uart_stream_write(&camera_stream, camera_tx_buffer, length);
}

/**
 * @brief  获取最近一次姿态角快照
 * @param  snapshot 接收姿态角数据的存储地址
 * @retval STATUS_OK 快照有效且已写入
 * @retval STATUS_INVALID_ARGUMENT snapshot 为空
 * @retval STATUS_UNAVAILABLE 尚未收到有效姿态数据
 */
status_code_t bsp_gyro_snapshot(bsp_gyro_snapshot_t *snapshot)
{
    driver_gyro_attitude_t attitude;

    if (!snapshot) {
        return STATUS_INVALID_ARGUMENT;
    }
    status_code_t status = driver_gyro_protocol_snapshot(&gyro_protocol, &attitude);
    snapshot->roll = attitude.roll;
    snapshot->pitch = attitude.pitch;
    snapshot->yaw = attitude.yaw;
    snapshot->is_valid = attitude.is_valid;
    snapshot->sequence = attitude.sequence;
    return status;
}

/**
 * @brief  获取板级单调毫秒时间
 * @return 自启动以来经过的毫秒数，允许自然回绕
 */
uint32_t bsp_time_get_ms(void)
{
    return HAL_GetTick();
}

/**
 * @brief  获取板级初始化和关键设备健康状态
 * @param  health 接收板级健康数据的存储地址
 * @retval STATUS_OK 板级组合根已经初始化
 * @retval STATUS_INVALID_ARGUMENT health 为空
 * @retval STATUS_NOT_INITIALIZED 板级组合根尚未完成初始化
 */
status_code_t bsp_board_health(bsp_board_health_t *health)
{
    if (!health) {
        return STATUS_INVALID_ARGUMENT;
    }
    health->is_initialized = is_board_initialized;
    health->is_motor_enabled = motor.is_enabled;
    health->is_sensor_valid = line_sensor.is_valid;
    health->optional_unavailable_mask = optional_unavailable_mask;
    health->timestamp_ms = HAL_GetTick();
    return is_board_initialized ? STATUS_OK : STATUS_NOT_INITIALIZED;
}
