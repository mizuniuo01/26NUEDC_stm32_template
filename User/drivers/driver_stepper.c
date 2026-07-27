/**
 * @file driver_stepper.c
 * @brief 编码并异步发送 ZDT X42S 串口步进电机命令。
 */
#include "driver_stepper.h"

/**
 * @brief  异步发送已经编码到实例缓冲区中的步进电机命令
 * @param  stepper 已初始化的步进电机驱动实例
 * @param  length 待发送字节数
 * @retval STATUS_OK 串口中断发送已启动
 * @retval STATUS_BUSY 上一次串口发送尚未完成
 * @retval STATUS_IO_ERROR HAL 无法启动串口中断发送
 */
static status_code_t send(driver_stepper_t *stepper, uint8_t length)
{
    if (stepper->is_busy) {
        return STATUS_BUSY;
    }
    stepper->is_busy = true;
    if (HAL_UART_Transmit_IT(stepper->config.uart, stepper->tx_buffer, length) != HAL_OK) {
        stepper->is_busy = false;
        return STATUS_IO_ERROR;
    }
    return STATUS_OK;
}

/**
 * @brief  初始化 ZDT X42S 串口步进电机驱动
 * @param  stepper 步进电机驱动实例
 * @param  config 串口句柄和电机总线 ID 配置
 * @retval STATUS_OK 驱动已初始化
 * @retval STATUS_INVALID_ARGUMENT 实例、配置或串口句柄为空
 */
status_code_t driver_stepper_init(driver_stepper_t *stepper, const driver_stepper_config_t *config)
{
    if (!stepper || !config || !config->uart) {
        return STATUS_INVALID_ARGUMENT;
    }
    stepper->config = *config;
    stepper->is_busy = false;
    stepper->is_enabled = false;
    stepper->is_initialized = true;
    return STATUS_OK;
}

/**
 * @brief  更新步进电机的软件使能状态
 * @param  stepper 步进电机驱动实例
 * @param  is_enabled 软件使能标志
 * @retval STATUS_OK 软件使能状态已更新
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 */
status_code_t driver_stepper_enable(driver_stepper_t *stepper, bool is_enabled)
{
    if (!stepper || !stepper->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    stepper->is_enabled = is_enabled;
    return STATUS_OK;
}

/**
 * @brief  编码并异步发送步进电机位置命令
 * @param  stepper 步进电机驱动实例
 * @param  pulses 目标脉冲数，符号表示方向
 * @param  speed 协议速度参数，大于 30000 时按 30000 发送
 * @param  is_absolute 绝对位置模式启用标志
 * @retval STATUS_OK 串口中断发送已启动
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 * @retval STATUS_STATE_ERROR 软件使能状态为禁用
 * @retval STATUS_BUSY 上一次串口发送尚未完成
 * @retval STATUS_IO_ERROR HAL 无法启动串口中断发送
 */
status_code_t driver_stepper_move(driver_stepper_t *stepper, int32_t pulses, uint16_t speed,
    bool is_absolute)
{
    uint32_t magnitude;

    if (!stepper || !stepper->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (!stepper->is_enabled) {
        return STATUS_STATE_ERROR;
    }
    if (speed > 30000U) {
        speed = 30000U;
    }
    magnitude = pulses < 0 ? (uint32_t)(-(pulses + 1)) + 1U : (uint32_t)pulses;
    stepper->tx_buffer[0] = stepper->config.id;
    stepper->tx_buffer[1] = 0xFDU;
    stepper->tx_buffer[2] = (uint8_t)(pulses < 0 ? 0x01U : 0x00U);
    /* ZDT X42S FD 帧依次包含地址、命令、方向、加减速、速度、大端位置、模式和校验。 */
    stepper->tx_buffer[3] = 0x01U;
    stepper->tx_buffer[4] = 0x2CU;
    stepper->tx_buffer[5] = 0x01U;
    stepper->tx_buffer[6] = 0x2CU;
    stepper->tx_buffer[7] = (uint8_t)(speed >> 8U);
    stepper->tx_buffer[8] = (uint8_t)speed;
    stepper->tx_buffer[9] = (uint8_t)(magnitude >> 24U);
    stepper->tx_buffer[10] = (uint8_t)(magnitude >> 16U);
    stepper->tx_buffer[11] = (uint8_t)(magnitude >> 8U);
    stepper->tx_buffer[12] = (uint8_t)magnitude;
    stepper->tx_buffer[13] = is_absolute ? 0x01U : 0x00U;
    stepper->tx_buffer[14] = 0x00U;
    stepper->tx_buffer[15] = 0x6BU;
    return send(stepper, 16U);
}

/**
 * @brief  处理步进电机串口发送完成事件
 * @param  stepper 步进电机驱动实例
 * @param  uart 发生完成事件的 STM32 HAL 串口句柄
 */
void driver_stepper_tx_complete_isr(driver_stepper_t *stepper, UART_HandleTypeDef *uart)
{
    if (stepper && uart && stepper->is_initialized &&
        (uart->Instance == stepper->config.uart->Instance)) {
        stepper->is_busy = false;
    }
}
