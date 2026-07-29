/**
 * @file driver_stepper.c
 * @brief 编码并异步发送 ZDT X42S 串口步进电机命令。
 */
#include "driver_stepper.h"

#define DRIVER_STEPPER_COMMAND_MOVE 0xFDU /* X 固件梯形曲线位置命令 */
#define DRIVER_STEPPER_COMMAND_ENABLE 0xF3U /* X 固件电机使能命令 */
#define DRIVER_STEPPER_COMMAND_STOP 0xFEU /* 电机立即停止命令 */
#define DRIVER_STEPPER_COMMAND_READ_POSITION 0x36U /* 读取实时位置命令 */
#define DRIVER_STEPPER_COMMAND_CLEAR_POSITION 0x0AU /* 将当前位置清零命令 */
#define DRIVER_STEPPER_ENABLE_AUXILIARY 0xABU /* 使能命令固定辅助码 */
#define DRIVER_STEPPER_STOP_AUXILIARY 0x98U /* 立即停止命令固定辅助码 */
#define DRIVER_STEPPER_CLEAR_POSITION_AUXILIARY 0x6DU /* 位置清零命令固定辅助码 */
#define DRIVER_STEPPER_CHECKSUM 0x6BU /* 固定通讯校验字节 */
#define DRIVER_STEPPER_MAX_SPEED 30000U /* 速度上限，单位 0.1 RPM */
#define DRIVER_STEPPER_COUNTS_PER_DEG 100.0F /* S_PosTDP Enable 时每度位置计数 */

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
 * @brief  尝试重新启动步进电机串口接收到空闲 DMA
 * @param  stepper 已初始化的步进电机驱动实例
 * @retval STATUS_OK 接收到空闲 DMA 已启动
 * @retval STATUS_IO_ERROR RX DMA 未配置或 HAL 无法启动接收
 */
static status_code_t start_receive(driver_stepper_t *stepper)
{
    HAL_StatusTypeDef hal_status;

    if (!stepper->config.uart->hdmarx) {
        return STATUS_IO_ERROR;
    }
    hal_status = HAL_UARTEx_ReceiveToIdle_DMA(stepper->config.uart, stepper->rx_dma_buffer,
        sizeof(stepper->rx_dma_buffer));
    if (hal_status != HAL_OK) {
        return STATUS_IO_ERROR;
    }
    __HAL_DMA_DISABLE_IT(stepper->config.uart->hdmarx, DMA_IT_HT);
    return STATUS_OK;
}

/**
 * @brief  将一个串口字节推进固定四字节控制命令应答解析
 * @param  stepper 已初始化的步进电机驱动实例
 * @param  byte 新收到的串口字节
 */
static void parse_response_byte(driver_stepper_t *stepper, uint8_t byte)
{
    uint32_t position_counts;

    if ((stepper->rx_length == 0U) && (byte != stepper->config.id)) {
        return;
    }
    stepper->rx_frame[stepper->rx_length++] = byte;
    if (stepper->rx_length == 2U) {
        stepper->rx_expected_length =
            byte == DRIVER_STEPPER_COMMAND_READ_POSITION
                ? DRIVER_STEPPER_POSITION_RESPONSE_SIZE
                : DRIVER_STEPPER_CONTROL_RESPONSE_SIZE;
    }
    if ((stepper->rx_expected_length == 0U) ||
        (stepper->rx_length < stepper->rx_expected_length)) {
        return;
    }
    if ((stepper->rx_frame[0] == stepper->config.id) &&
        (stepper->rx_frame[stepper->rx_expected_length - 1U] == DRIVER_STEPPER_CHECKSUM)) {
        if (stepper->rx_frame[1] == DRIVER_STEPPER_COMMAND_READ_POSITION) {
            if (stepper->rx_frame[2] <= 0x01U) {
                position_counts = ((uint32_t)stepper->rx_frame[3] << 24U) |
                                  ((uint32_t)stepper->rx_frame[4] << 16U) |
                                  ((uint32_t)stepper->rx_frame[5] << 8U) |
                                  (uint32_t)stepper->rx_frame[6];
                stepper->position_sign = stepper->rx_frame[2];
                stepper->position_counts = position_counts;
                stepper->position_sequence++;
                stepper->position_valid = true;
            }
        } else {
            stepper->response_command = stepper->rx_frame[1];
            stepper->response_code = stepper->rx_frame[2];
            stepper->response_sequence++;
            stepper->response_valid = true;
        }
    }
    stepper->rx_length = byte == stepper->config.id ? 1U : 0U;
    stepper->rx_expected_length = 0U;
    if (stepper->rx_length != 0U) {
        stepper->rx_frame[0] = byte;
    }
}

/**
 * @brief  初始化 ZDT X42S 串口步进电机驱动
 * @param  stepper 步进电机驱动实例
 * @param  config 串口句柄和电机总线 ID 配置
 * @retval STATUS_OK 驱动已初始化
 * @retval STATUS_INVALID_ARGUMENT 实例、配置、串口句柄为空或地址为广播地址 0
 * @retval STATUS_IO_ERROR 串口未配置收发 DMA 或 HAL 无法启动 DMA 接收
 */
status_code_t driver_stepper_init(driver_stepper_t *stepper, const driver_stepper_config_t *config)
{
    if (!stepper || !config || !config->uart || (config->id == 0U)) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!config->uart->hdmatx || !config->uart->hdmarx) {
        return STATUS_IO_ERROR;
    }
    stepper->config = *config;
    stepper->is_busy = false;
    stepper->response_sequence = 0U;
    stepper->position_sequence = 0U;
    stepper->position_counts = 0U;
    stepper->response_command = 0U;
    stepper->response_code = 0U;
    stepper->position_sign = 0U;
    stepper->rx_length = 0U;
    stepper->rx_expected_length = 0U;
    stepper->response_valid = false;
    stepper->position_valid = false;
    stepper->is_enabled = false;
    stepper->is_initialized = true;
    if (start_receive(stepper) != STATUS_OK) {
        stepper->is_initialized = false;
        return STATUS_IO_ERROR;
    }
    return STATUS_OK;
}

/**
 * @brief  通过 X 固件 F3 AB 命令更新步进电机使能状态
 * @param  stepper 步进电机驱动实例
 * @param  is_enabled 目标使能状态；未接 EN 脚时也通过 UART 生效
 * @retval STATUS_OK 使能命令的 UART DMA 发送已启动
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 * @retval STATUS_BUSY 上一条串口命令尚未发送完成
 * @retval STATUS_IO_ERROR 未配置发送 DMA 或 HAL 无法启动 DMA 发送
 */
status_code_t driver_stepper_enable(driver_stepper_t *stepper, bool is_enabled)
{
    status_code_t status;

    if (!stepper || !stepper->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (stepper->is_busy) {
        return STATUS_BUSY;
    }
    /* 手册 5.3.2：Addr F3 AB 00/01 00 6B。同步标志固定为立即执行。 */
    stepper->tx_buffer[0] = stepper->config.id;
    stepper->tx_buffer[1] = DRIVER_STEPPER_COMMAND_ENABLE;
    stepper->tx_buffer[2] = DRIVER_STEPPER_ENABLE_AUXILIARY;
    stepper->tx_buffer[3] = is_enabled ? 0x01U : 0x00U;
    stepper->tx_buffer[4] = 0x00U;
    stepper->tx_buffer[5] = DRIVER_STEPPER_CHECKSUM;
    status = send(stepper, 6U);
    if (status == STATUS_OK) {
        /* 只有 HAL 接受了命令后才更新软件状态，避免 DMA 忙时状态漂移。 */
        stepper->is_enabled = is_enabled;
    }
    return status;
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
    if (stepper->is_busy) {
        return STATUS_BUSY;
    }
    if ((mode != DRIVER_STEPPER_MODE_RELATIVE_TARGET) && (mode != DRIVER_STEPPER_MODE_ABSOLUTE) &&
        (mode != DRIVER_STEPPER_MODE_RELATIVE_CURRENT)) {
        return STATUS_OUT_OF_RANGE;
    }
    absolute_angle = angle < 0.0F ? -(double)angle : (double)angle;
    scaled_angle = absolute_angle * DRIVER_STEPPER_COUNTS_PER_DEG;
    /* S_PosTDP 已启用，位置字段是无符号 0.01° 计数，0° 是合法位置。 */
    if ((angle != angle) || (scaled_angle < 0.0) || (scaled_angle > ((double)UINT32_MAX - 0.5))) {
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
 * @brief  通过 FE 98 命令立即停止步进电机运动
 * @param  stepper 步进电机驱动实例
 * @retval STATUS_OK 停止命令的 UART DMA 发送已启动
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 * @retval STATUS_BUSY 上一条串口命令尚未发送完成
 * @retval STATUS_IO_ERROR 未配置发送 DMA 或 HAL 无法启动 DMA 发送
 */
status_code_t driver_stepper_stop(driver_stepper_t *stepper)
{
    if (!stepper || !stepper->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (stepper->is_busy) {
        return STATUS_BUSY;
    }
    stepper->tx_buffer[0] = stepper->config.id;
    stepper->tx_buffer[1] = DRIVER_STEPPER_COMMAND_STOP;
    stepper->tx_buffer[2] = DRIVER_STEPPER_STOP_AUXILIARY;
    stepper->tx_buffer[3] = 0x00U;
    stepper->tx_buffer[4] = DRIVER_STEPPER_CHECKSUM;
    return send(stepper, 5U);
}

/**
 * @brief  通过 0A 6D 命令将步进电机当前位置设为坐标零点
 * @param  stepper 步进电机驱动实例
 * @retval STATUS_OK 位置清零命令的 UART DMA 发送已启动
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 * @retval STATUS_BUSY 上一条串口命令尚未发送完成
 * @retval STATUS_IO_ERROR 未配置发送 DMA 或 HAL 无法启动 DMA 发送
 */
status_code_t driver_stepper_clear_position(driver_stepper_t *stepper)
{
    if (!stepper || !stepper->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (stepper->is_busy) {
        return STATUS_BUSY;
    }
    stepper->tx_buffer[0] = stepper->config.id;
    stepper->tx_buffer[1] = DRIVER_STEPPER_COMMAND_CLEAR_POSITION;
    stepper->tx_buffer[2] = DRIVER_STEPPER_CLEAR_POSITION_AUXILIARY;
    stepper->tx_buffer[3] = DRIVER_STEPPER_CHECKSUM;
    return send(stepper, 4U);
}

/**
 * @brief  发送 36 命令读取步进电机实时位置
 * @param  stepper 步进电机驱动实例
 * @retval STATUS_OK 读取位置命令的 UART DMA 发送已启动
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 * @retval STATUS_BUSY 上一条串口命令尚未发送完成
 * @retval STATUS_IO_ERROR 未配置发送 DMA 或 HAL 无法启动 DMA 发送
 */
status_code_t driver_stepper_read_position(driver_stepper_t *stepper)
{
    if (!stepper || !stepper->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (stepper->is_busy) {
        return STATUS_BUSY;
    }
    stepper->tx_buffer[0] = stepper->config.id;
    stepper->tx_buffer[1] = DRIVER_STEPPER_COMMAND_READ_POSITION;
    stepper->tx_buffer[2] = DRIVER_STEPPER_CHECKSUM;
    return send(stepper, 3U);
}

/**
 * @brief  原子读取最近一次有效的步进电机控制命令应答
 * @param  stepper 步进电机驱动实例
 * @param  response 接收应答快照的存储地址
 * @retval STATUS_OK 应答快照已写入
 * @retval STATUS_INVALID_ARGUMENT response 为空
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 * @retval STATUS_UNAVAILABLE 尚未收到有效应答
 */
status_code_t driver_stepper_response_snapshot(driver_stepper_t *stepper,
    driver_stepper_response_t *response)
{
    uint32_t interrupt_mask;

    if (!response) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!stepper || !stepper->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    interrupt_mask = __get_PRIMASK();
    __disable_irq();
    response->command = stepper->response_command;
    response->code = stepper->response_code;
    response->sequence = stepper->response_sequence;
    response->is_valid = stepper->response_valid;
    __set_PRIMASK(interrupt_mask);
    return response->is_valid ? STATUS_OK : STATUS_UNAVAILABLE;
}

/**
 * @brief  原子读取最近一次有效的步进电机实时位置
 * @param  stepper 步进电机驱动实例
 * @param  position 接收位置快照的存储地址
 * @retval STATUS_OK 位置快照已写入
 * @retval STATUS_INVALID_ARGUMENT position 为空
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 * @retval STATUS_UNAVAILABLE 尚未收到有效位置应答
 */
status_code_t driver_stepper_position_snapshot(driver_stepper_t *stepper,
    driver_stepper_position_t *position)
{
    uint32_t interrupt_mask;
    uint32_t position_counts;
    uint8_t sign;

    if (!position) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!stepper || !stepper->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    interrupt_mask = __get_PRIMASK();
    __disable_irq();
    position_counts = stepper->position_counts;
    sign = stepper->position_sign;
    position->sequence = stepper->position_sequence;
    position->is_valid = stepper->position_valid;
    __set_PRIMASK(interrupt_mask);
    position->angle_deg = (float)position_counts / DRIVER_STEPPER_COUNTS_PER_DEG;
    if (sign == 0x01U) {
        position->angle_deg = -position->angle_deg;
    }
    return position->is_valid ? STATUS_OK : STATUS_UNAVAILABLE;
}

/**
 * @brief  解析一次步进电机串口接收到空闲事件并重新启动 DMA
 * @param  stepper 步进电机驱动实例
 * @param  uart 发生接收事件的 STM32 HAL 串口句柄
 * @param  size 当前 DMA 缓冲区收到的字节数
 */
void driver_stepper_rx_event_isr(driver_stepper_t *stepper, UART_HandleTypeDef *uart,
    uint16_t size)
{
    uint16_t index;
    uint16_t length;

    if (!stepper || !uart || !stepper->is_initialized ||
        (uart->Instance != stepper->config.uart->Instance)) {
        return;
    }
    length = size > sizeof(stepper->rx_dma_buffer) ? sizeof(stepper->rx_dma_buffer) : size;
    for (index = 0U; index < length; index++) {
        parse_response_byte(stepper, stepper->rx_dma_buffer[index]);
    }
    (void)start_receive(stepper);
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
        (void)start_receive(stepper);
    }
}
