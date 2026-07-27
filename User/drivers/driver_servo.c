/**
 * @file driver_servo.c
 * @brief 编码并异步发送 FashionStar 总线舵机角度命令。
 */
#include "driver_servo.h"

/**
 * @brief  初始化 FashionStar 总线舵机驱动
 * @param  servo 舵机驱动实例
 * @param  config 串口、允许 ID、动作时间和功率配置
 * @retval STATUS_OK 驱动已初始化
 * @retval STATUS_INVALID_ARGUMENT 参数或串口句柄为空，或 ID 数量超出容量
 */
status_code_t driver_servo_init(driver_servo_t *servo, const driver_servo_config_t *config)
{
    if (!servo || !config || !config->uart || (config->count == 0U) ||
        (config->count > 3U)) {
        return STATUS_INVALID_ARGUMENT;
    }
    servo->config = *config;
    servo->is_busy = false;
    servo->is_initialized = true;
    return STATUS_OK;
}

/**
 * @brief  向指定舵机异步发送目标角度命令
 * @param  servo 舵机驱动实例
 * @param  id 舵机总线 ID
 * @param  angle 目标角度，单位：度，范围 [-180, 180]
 * @retval STATUS_OK 串口中断发送已启动
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 * @retval STATUS_BUSY 上一次串口发送尚未完成
 * @retval STATUS_OUT_OF_RANGE id 未登记或角度超出范围
 * @retval STATUS_IO_ERROR HAL 无法启动串口中断发送
 */
status_code_t driver_servo_set_angle(driver_servo_t *servo, uint8_t id, float angle)
{
    int16_t angle_tenth;
    uint8_t checksum;
    uint8_t i;
    bool has_known_id = false;

    if (!servo || !servo->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (servo->is_busy) {
        return STATUS_BUSY;
    }
    for (i = 0U; i < servo->config.count; i++) {
        if (servo->config.ids[i] == id) {
            has_known_id = true;
            break;
        }
    }
    if (!has_known_id) {
        return STATUS_OUT_OF_RANGE;
    }
    if ((angle < -180.0f) || (angle > 180.0f)) {
        return STATUS_OUT_OF_RANGE;
    }
    angle_tenth = (int16_t)(angle * 10.0f);
    /* FashionStar FSUS 帧包含 0x4C12 帧头、旋转命令和 7 字节负载。 */
    servo->tx_buffer[0] = 0x12U;
    servo->tx_buffer[1] = 0x4CU;
    servo->tx_buffer[2] = 0x08U;
    servo->tx_buffer[3] = 0x07U;
    servo->tx_buffer[4] = id;
    servo->tx_buffer[5] = (uint8_t)angle_tenth;
    servo->tx_buffer[6] = (uint8_t)((uint16_t)angle_tenth >> 8U);
    servo->tx_buffer[7] = (uint8_t)servo->config.interval_ms;
    servo->tx_buffer[8] = (uint8_t)(servo->config.interval_ms >> 8U);
    servo->tx_buffer[9] = (uint8_t)servo->config.power;
    servo->tx_buffer[10] = (uint8_t)(servo->config.power >> 8U);
    /* FSUS 使用保留低八位的累加校验和。 */
    checksum = 0U;
    for (i = 0U; i < 11U; i++) {
        checksum = (uint8_t)(checksum + servo->tx_buffer[i]);
    }
    servo->tx_buffer[11] = checksum;
    servo->is_busy = true;
    if (HAL_UART_Transmit_IT(servo->config.uart, servo->tx_buffer, 12U) != HAL_OK) {
        servo->is_busy = false;
        return STATUS_IO_ERROR;
    }
    return STATUS_OK;
}

/**
 * @brief  处理舵机串口发送完成事件
 * @param  servo 舵机驱动实例
 * @param  uart 发生完成事件的 STM32 HAL 串口句柄
 */
void driver_servo_tx_complete_isr(driver_servo_t *servo, UART_HandleTypeDef *uart)
{
    if (servo && uart && servo->is_initialized &&
        (uart->Instance == servo->config.uart->Instance)) {
        servo->is_busy = false;
    }
}

/**
 * @brief  处理舵机串口错误并释放发送状态
 * @param  servo 舵机驱动实例
 * @param  uart 发生错误事件的 STM32 HAL 串口句柄
 */
void driver_servo_error_isr(driver_servo_t *servo, UART_HandleTypeDef *uart)
{
    if (servo && uart && servo->is_initialized &&
        (uart->Instance == servo->config.uart->Instance)) {
        servo->is_busy = false;
    }
}
