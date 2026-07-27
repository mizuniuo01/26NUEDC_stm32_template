/**
 * @file driver_stepper.c
 * @brief 编码并异步发送 ZDT X42S 串口步进电机命令。
 */
#include "driver_stepper.h"

#define DRIVER_STEPPER_COMMAND_MOVE 0xFDU
#define DRIVER_STEPPER_CHECKSUM 0x6BU
#define DRIVER_STEPPER_MAX_SPEED 30000U
#define DRIVER_STEPPER_ANGLE_SCALE 100.0

/**
 * @brief  异步发送已经编码到实例缓冲区中的步进电机命令
 * @param  stepper 已初始化的步进电机驱动实例
 * @param  length 待发送字节数
 * @retval STATUS_OK 串口 DMA 发送已启动
 * @retval STATUS_BUSY 上一次串口发送尚未完成
 * @retval STATUS_IO_ERROR 未配置发送 DMA 或 HAL 无法启动 DMA 发送
 */
static status_code_t send(driver_stepper_t *stepper, uint8_t length)
{
    HAL_StatusTypeDef hal_status;

    if (stepper->is_busy) {
        return STATUS_BUSY;
    }
    if (!stepper->config.uart->hdmatx) {
        return STATUS_IO_ERROR;
    }
    stepper->is_busy = true;
    hal_status = HAL_UART_Transmit_DMA(stepper->config.uart, stepper->tx_buffer, length);
    if (hal_status != HAL_OK) {
        stepper->is_busy = false;
        return hal_status == HAL_BUSY ? STATUS_BUSY : STATUS_IO_ERROR;
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
 * @param  angle 目标角度，符号表示方向，按原协议放大 100 倍发送
 * @param  speed 协议速度参数，大于 30000 时按 30000 发送
 * @param  acceleration 加速和减速使用的加速度参数
 * @param  mode 位置模式
 * @param  is_synchronized 等待多机同步触发标志
 * @retval STATUS_OK 串口 DMA 发送已启动
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 * @retval STATUS_STATE_ERROR 软件使能状态为禁用
 * @retval STATUS_OUT_OF_RANGE 角度或位置模式无效
 * @retval STATUS_BUSY 上一次串口发送尚未完成
 * @retval STATUS_IO_ERROR 未配置发送 DMA 或 HAL 无法启动 DMA 发送
 */
status_code_t driver_stepper_move(driver_stepper_t *stepper, float angle, uint16_t speed,
    uint16_t acceleration, driver_stepper_move_mode_t mode, bool is_synchronized)
{
    double absolute_angle;
    double scaled_angle;
    uint32_t magnitude;

    if (!stepper || !stepper->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (!stepper->is_enabled) {
        return STATUS_STATE_ERROR;
    }
    if ((mode != DRIVER_STEPPER_MODE_RELATIVE_TARGET) &&
        (mode != DRIVER_STEPPER_MODE_ABSOLUTE) &&
        (mode != DRIVER_STEPPER_MODE_RELATIVE_CURRENT)) {
        return STATUS_OUT_OF_RANGE;
    }
    absolute_angle = angle < 0.0F ? -(double)angle : (double)angle;
    scaled_angle = absolute_angle * DRIVER_STEPPER_ANGLE_SCALE;
    if ((angle != angle) || (scaled_angle < 0.5) || (scaled_angle > (double)UINT32_MAX)) {
        return STATUS_OUT_OF_RANGE;
    }
    if (speed > DRIVER_STEPPER_MAX_SPEED) {
        speed = DRIVER_STEPPER_MAX_SPEED;
    }
    magnitude = (uint32_t)(scaled_angle + 0.5);
    stepper->tx_buffer[0] = stepper->config.id;
    stepper->tx_buffer[1] = DRIVER_STEPPER_COMMAND_MOVE;
    stepper->tx_buffer[2] = (uint8_t)(angle < 0.0F ? 0x01U : 0x00U);
    /* ZDT X42S FD 帧依次包含地址、命令、方向、加减速、速度、大端位置、模式和校验。 */
    stepper->tx_buffer[3] = (uint8_t)(acceleration >> 8U);
    stepper->tx_buffer[4] = (uint8_t)acceleration;
    stepper->tx_buffer[5] = (uint8_t)(acceleration >> 8U);
    stepper->tx_buffer[6] = (uint8_t)acceleration;
    stepper->tx_buffer[7] = (uint8_t)(speed >> 8U);
    stepper->tx_buffer[8] = (uint8_t)speed;
    stepper->tx_buffer[9] = (uint8_t)(magnitude >> 24U);
    stepper->tx_buffer[10] = (uint8_t)(magnitude >> 16U);
    stepper->tx_buffer[11] = (uint8_t)(magnitude >> 8U);
    stepper->tx_buffer[12] = (uint8_t)magnitude;
    stepper->tx_buffer[13] = (uint8_t)mode;
    stepper->tx_buffer[14] = is_synchronized ? 0x01U : 0x00U;
    stepper->tx_buffer[15] = DRIVER_STEPPER_CHECKSUM;
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

/**
 * @brief  处理步进电机串口错误并释放发送状态
 * @param  stepper 步进电机驱动实例
 * @param  uart 发生错误事件的 STM32 HAL 串口句柄
 */
void driver_stepper_error_isr(driver_stepper_t *stepper, UART_HandleTypeDef *uart)
{
    if (stepper && uart && stepper->is_initialized &&
        (uart->Instance == stepper->config.uart->Instance)) {
        stepper->is_busy = false;
    }
}
