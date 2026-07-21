/**
 * @file    cam.c
 * @brief   MaixCAM 二进制串口通信驱动。
 * @author  mizuniuo01
 * @date    2026-07-22
 * @version 3.0.0
 * @note    线上帧为 FF + 转义后的版本/类型/长度/负载/CRC8 + FE。
 * @note    USART3 使用 DMA Receive-to-Idle 接收和 DMA 发送队列。
 * @warning HAL 回调只搬运数据和置位，解析与下一帧发送由 cam_task() 推进。
 */

#include "cam.h"
#include "error_handler.h"

#include <string.h>

/** @brief 二进制协议字段长度和 CRC 参数。 */
typedef enum {
    CAM_PROTOCOL_PREFIX_SIZE = 3U,       /**< 版本、类型、长度字段总长度。 */
    CAM_PROTOCOL_CRC_SIZE = 1U,          /**< CRC8 字段长度。 */
    CAM_TARGET_REPORT_PAYLOAD_SIZE = 6U, /**< 目标报告负载长度。 */
    CAM_SWITCH_COMMAND_PAYLOAD_SIZE = 2U, /**< 切换命令负载长度。 */
    CAM_TARGET_FLAG_MASK = 0x03U,         /**< 当前定义的目标标志掩码。 */
    CAM_CRC8_POLYNOMIAL = 0x07U,          /**< CRC-8 多项式。 */
} cam_protocol_value_t;

/** @brief 摄像头通信模块的单实例状态。 */
static cam_handle_t cam_instance;

/** @brief 最近一次有效目标报告。 */
static cam_data_t cam_data;

/** @brief 非零表示存在尚未被上层消费的新目标报告。 */
static uint8_t cam_data_ready;

/** @brief 目标切换 ACK 事件锁存。 */
static uint8_t cam_switch_ack_latched;

/** @brief 最近一次锁存的目标切换请求 ID。 */
static uint8_t cam_switch_ack_id;

/**
 * @brief  将 HAL UART 状态转换为摄像头驱动状态。
 * @param  hal_status  HAL UART 返回状态。
 * @return 对应的摄像头驱动状态。
 */
static cam_status_t cam_map_hal_status(HAL_StatusTypeDef hal_status)
{
    if (hal_status == HAL_OK) {
        return CAM_STATUS_OK;
    }
    if (hal_status == HAL_BUSY) {
        error_report(ERROR_SOURCE_CAM, DRV_ERR_BUSY);
        return CAM_STATUS_BUSY;
    }

    error_report(ERROR_SOURCE_CAM, DRV_ERR_IO);
    return CAM_STATUS_IO_ERROR;
}

/**
 * @brief  计算协议使用的 CRC-8。
 * @param  data    参与计算的数据。
 * @param  length  数据长度。
 * @return CRC-8 结果。
 */
static uint8_t cam_calculate_crc8(const uint8_t *data, uint16_t length)
{
    uint8_t crc = 0U;
    uint16_t index;
    uint8_t bit;

    for (index = 0U; index < length; index++) {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++) {
            crc = (crc & 0x80U)
                      ? (uint8_t)((crc << 1U) ^ CAM_CRC8_POLYNOMIAL)
                      : (uint8_t)(crc << 1U);
        }
    }
    return crc;
}

/**
 * @brief  向发送帧写入一个经过必要转义的字节。
 * @param  frame        发送帧。
 * @param  frame_index  当前写入位置。
 * @param  byte         原始逻辑帧字节。
 * @return 写入成功返回 1，容量不足返回 0。
 */
static uint8_t cam_append_escaped_byte(
    cam_tx_frame_t *frame,
    uint16_t *frame_index,
    uint8_t byte)
{
    uint8_t escaped_byte;

    if ((frame == NULL) || (frame_index == NULL)) {
        return 0U;
    }

    if ((byte == CAM_FRAME_HEADER) || (byte == CAM_FRAME_TAIL)
        || (byte == CAM_ESCAPE_BYTE)) {
        if ((*frame_index + 2U) > CAM_TX_FRAME_MAX_SIZE) {
            return 0U;
        }
        escaped_byte = (byte == CAM_FRAME_HEADER)
                           ? CAM_ESCAPE_HEADER
                           : ((byte == CAM_FRAME_TAIL) ? CAM_ESCAPE_TAIL
                                                       : CAM_ESCAPE_ESCAPE);
        frame->data[(*frame_index)++] = CAM_ESCAPE_BYTE;
        frame->data[(*frame_index)++] = escaped_byte;
        return 1U;
    }

    if ((*frame_index + 1U) > CAM_TX_FRAME_MAX_SIZE) {
        return 0U;
    }
    frame->data[(*frame_index)++] = byte;
    return 1U;
}

/**
 * @brief  获取当前可写发送队列槽位。
 * @return 可写槽位；未初始化或队列满时返回 NULL。
 */
static cam_tx_frame_t *cam_get_tx_write_frame(void)
{
    if (cam_instance.huart == NULL) {
        cam_instance.last_status = CAM_STATUS_NOT_INITIALIZED;
        error_report(ERROR_SOURCE_CAM, DRV_ERR_STATE);
        return NULL;
    }
    if (cam_instance.tx_count >= CAM_TX_QUEUE_CAPACITY) {
        cam_instance.last_status = CAM_STATUS_QUEUE_FULL;
        error_report(ERROR_SOURCE_CAM, DRV_ERR_BUSY);
        return NULL;
    }

    return &cam_instance.tx_queue[cam_instance.tx_write_pos];
}

/**
 * @brief  发布已经写完的发送帧。
 * @param  length  完整线上帧长度。
 * @return 无。
 */
static void cam_publish_tx_frame(uint16_t length)
{
    uint32_t interrupt_mask;

    cam_instance.tx_queue[cam_instance.tx_write_pos].length = length;
    interrupt_mask = __get_PRIMASK();
    __disable_irq();
    cam_instance.tx_write_pos =
        (uint8_t)((cam_instance.tx_write_pos + 1U) % CAM_TX_QUEUE_CAPACITY);
    cam_instance.tx_count++;
    __set_PRIMASK(interrupt_mask);
    cam_instance.last_status = CAM_STATUS_OK;
}

/**
 * @brief  构造并加入一帧二进制消息。
 * @param  message_type  消息类型。
 * @param  payload       负载。
 * @param  payload_size  负载长度。
 * @return 入队结果。
 */
static cam_status_t cam_send_message(
    cam_message_type_t message_type,
    const uint8_t *payload,
    uint8_t payload_size)
{
    uint8_t logical_frame[CAM_LOGICAL_FRAME_MAX_SIZE];
    cam_tx_frame_t *tx_frame;
    uint16_t logical_length;
    uint16_t logical_index;
    uint16_t tx_index;

    if (((payload == NULL) && (payload_size > 0U))
        || (payload_size > CAM_MAX_PAYLOAD_SIZE)) {
        return CAM_STATUS_INVALID_ARGUMENT;
    }

    tx_frame = cam_get_tx_write_frame();
    if (tx_frame == NULL) {
        return cam_instance.last_status;
    }

    logical_frame[0] = CAM_PROTOCOL_VERSION;
    logical_frame[1] = (uint8_t)message_type;
    logical_frame[2] = payload_size;
    if (payload_size > 0U) {
        memcpy(&logical_frame[CAM_PROTOCOL_PREFIX_SIZE], payload, payload_size);
    }
    logical_length = (uint16_t)(CAM_PROTOCOL_PREFIX_SIZE + payload_size);
    logical_frame[logical_length] = cam_calculate_crc8(logical_frame, logical_length);
    logical_length += CAM_PROTOCOL_CRC_SIZE;

    tx_index = 0U;
    tx_frame->data[tx_index++] = CAM_FRAME_HEADER;
    for (logical_index = 0U; logical_index < logical_length; logical_index++) {
        if (!cam_append_escaped_byte(tx_frame, &tx_index, logical_frame[logical_index])) {
            return CAM_STATUS_INVALID_ARGUMENT;
        }
    }
    tx_frame->data[tx_index++] = CAM_FRAME_TAIL;
    cam_publish_tx_frame(tx_index);
    return CAM_STATUS_OK;
}

/**
 * @brief  解析一帧目标报告负载。
 * @param  payload  目标报告负载。
 * @param  length   负载长度。
 * @return 格式合法返回 1，否则返回 0。
 */
static uint8_t cam_parse_target_report(const uint8_t *payload, uint8_t length)
{
    uint8_t flags;

    if ((payload == NULL) || (length != CAM_TARGET_REPORT_PAYLOAD_SIZE)) {
        return 0U;
    }

    flags = payload[0];
    if ((flags & (uint8_t)(~CAM_TARGET_FLAG_MASK)) != 0U) {
        return 0U;
    }

    cam_data.has_target = (flags & CAM_TARGET_FLAG_HAS_TARGET) ? 1U : 0U;
    cam_data.switch_ack = (flags & CAM_TARGET_FLAG_SWITCH_ACK) ? 1U : 0U;
    cam_data.switch_ack_id = payload[5];
    if (cam_data.has_target) {
        cam_data.error_x =
            (int16_t)((uint16_t)payload[1] | ((uint16_t)payload[2] << 8U));
        cam_data.error_y =
            (int16_t)((uint16_t)payload[3] | ((uint16_t)payload[4] << 8U));
    } else {
        cam_data.error_x = 0;
        cam_data.error_y = 0;
    }

    if (!cam_data.switch_ack) {
        cam_data.switch_ack_id = 0U;
    } else {
        cam_switch_ack_id = cam_data.switch_ack_id;
        cam_switch_ack_latched = 1U;
    }
    cam_data_ready = 1U;
    return 1U;
}

/**
 * @brief  校验并分发一帧完整逻辑帧。
 * @param  frame   不含 FF/FE 的逻辑帧。
 * @param  length  逻辑帧长度。
 * @return 无。
 */
static void cam_process_logical_frame(const uint8_t *frame, uint16_t length)
{
    uint8_t payload_size;
    uint16_t expected_length;

    if ((frame == NULL) || (length < (CAM_PROTOCOL_PREFIX_SIZE + CAM_PROTOCOL_CRC_SIZE))) {
        return;
    }

    payload_size = frame[2];
    expected_length =
        (uint16_t)(CAM_PROTOCOL_PREFIX_SIZE + payload_size + CAM_PROTOCOL_CRC_SIZE);
    if ((frame[0] != CAM_PROTOCOL_VERSION) || (length != expected_length)
        || (cam_calculate_crc8(frame, (uint16_t)(length - 1U)) != frame[length - 1U])) {
        return;
    }

    if (frame[1] == CAM_MESSAGE_TARGET_REPORT) {
        (void)cam_parse_target_report(&frame[CAM_PROTOCOL_PREFIX_SIZE], payload_size);
    }
}

/**
 * @brief  将一个解码后的逻辑字节加入当前接收帧。
 * @param  byte  解码后的字节。
 * @return 写入成功返回 1，帧超长并复位解析器时返回 0。
 */
static uint8_t cam_append_received_byte(uint8_t byte)
{
    if (cam_instance.logical_frame_length >= CAM_LOGICAL_FRAME_MAX_SIZE) {
        cam_instance.logical_frame_length = 0U;
        cam_instance.parse_state = CAM_PARSE_WAIT_HEADER;
        return 0U;
    }

    cam_instance.logical_frame[cam_instance.logical_frame_length++] = byte;
    return 1U;
}

/**
 * @brief  解析接收 FIFO 中的全部协议字节。
 * @return 无。
 */
static void cam_parse_received_frames(void)
{
    uint8_t byte;

    while (cam_instance.rx_read_pos != cam_instance.rx_write_pos) {
        byte = cam_instance.rx_fifo[cam_instance.rx_read_pos];
        cam_instance.rx_read_pos =
            (uint16_t)((cam_instance.rx_read_pos + 1U) % CAM_RX_FIFO_SIZE);

        switch (cam_instance.parse_state) {
            case CAM_PARSE_WAIT_HEADER:
                if (byte == CAM_FRAME_HEADER) {
                    cam_instance.logical_frame_length = 0U;
                    cam_instance.parse_state = CAM_PARSE_RECEIVING;
                }
                break;

            case CAM_PARSE_RECEIVING:
                if (byte == CAM_FRAME_TAIL) {
                    cam_process_logical_frame(
                        cam_instance.logical_frame,
                        cam_instance.logical_frame_length);
                    cam_instance.logical_frame_length = 0U;
                    cam_instance.parse_state = CAM_PARSE_WAIT_HEADER;
                } else if (byte == CAM_FRAME_HEADER) {
                    cam_instance.logical_frame_length = 0U;
                } else if (byte == CAM_ESCAPE_BYTE) {
                    cam_instance.parse_state = CAM_PARSE_ESCAPE;
                } else {
                    (void)cam_append_received_byte(byte);
                }
                break;

            case CAM_PARSE_ESCAPE:
                if (byte == CAM_ESCAPE_HEADER) {
                    if (cam_append_received_byte(CAM_FRAME_HEADER)) {
                        cam_instance.parse_state = CAM_PARSE_RECEIVING;
                    }
                } else if (byte == CAM_ESCAPE_TAIL) {
                    if (cam_append_received_byte(CAM_FRAME_TAIL)) {
                        cam_instance.parse_state = CAM_PARSE_RECEIVING;
                    }
                } else if (byte == CAM_ESCAPE_ESCAPE) {
                    if (cam_append_received_byte(CAM_ESCAPE_BYTE)) {
                        cam_instance.parse_state = CAM_PARSE_RECEIVING;
                    }
                } else {
                    cam_instance.logical_frame_length = 0U;
                    cam_instance.parse_state = CAM_PARSE_WAIT_HEADER;
                }
                break;

            default:
                cam_instance.logical_frame_length = 0U;
                cam_instance.parse_state = CAM_PARSE_WAIT_HEADER;
                break;
        }
    }
}

/**
 * @brief  在 UART 空闲时启动队首帧的 DMA 发送。
 * @return 无。
 */
static void cam_start_next_transmission(void)
{
    cam_tx_frame_t *frame;
    HAL_StatusTypeDef hal_status;

    if ((cam_instance.huart == NULL) || (cam_instance.is_tx_busy != 0U)
        || (cam_instance.tx_count == 0U)) {
        return;
    }

    frame = &cam_instance.tx_queue[cam_instance.tx_read_pos];
    hal_status = HAL_UART_Transmit_DMA(cam_instance.huart, frame->data, frame->length);
    cam_instance.last_status = cam_map_hal_status(hal_status);
    if (hal_status == HAL_OK) {
        cam_instance.is_tx_busy = 1U;
    }
}

/**
 * @brief  初始化摄像头串口通信并启动 DMA 空闲接收。
 * @param  huart  已配置 USART3 RX/TX DMA 的 UART 句柄。
 * @return 无。
 */
void cam_init(UART_HandleTypeDef *huart)
{
    HAL_StatusTypeDef hal_status;

    if (huart == NULL) {
        error_report(ERROR_SOURCE_CAM, DRV_ERR_PARAM);
        return;
    }

    memset(&cam_instance, 0, sizeof(cam_instance));
    memset(&cam_data, 0, sizeof(cam_data));
    cam_instance.huart = huart;
    cam_instance.parse_state = CAM_PARSE_WAIT_HEADER;
    cam_data_ready = 0U;
    cam_switch_ack_latched = 0U;
    cam_switch_ack_id = 0U;
    hal_status = HAL_UARTEx_ReceiveToIdle_DMA(
        huart,
        cam_instance.dma_rx_buffer,
        CAM_DMA_RX_BUF_SIZE);
    cam_instance.last_status = cam_map_hal_status(hal_status);
}

/**
 * @brief  处理摄像头接收数据并推进 DMA 发送队列。
 * @return 无。
 */
void cam_task(void)
{
    cam_parse_received_frames();
    cam_start_next_transmission();
}

/**
 * @brief  获取最近一次有效目标报告。
 * @return 目标报告快照。
 */
cam_data_t cam_get_data(void)
{
    return cam_data;
}

/**
 * @brief  读取并消费最新目标报告事件。
 * @param  data  目标报告输出地址。
 * @return 存在新报告返回 1，否则返回 0。
 */
uint8_t cam_take_data(cam_data_t *data)
{
    if ((data == NULL) || (cam_data_ready == 0U)) {
        return 0U;
    }

    *data = cam_data;
    cam_data_ready = 0U;
    return 1U;
}

/**
 * @brief  读取并清除目标切换 ACK 事件。
 * @param  request_id  ACK 请求 ID 输出地址。
 * @return 存在尚未消费的 ACK 返回 1，否则返回 0。
 */
uint8_t cam_take_switch_ack(uint8_t *request_id)
{
    if ((request_id == NULL) || (cam_switch_ack_latched == 0U)) {
        return 0U;
    }

    *request_id = cam_switch_ack_id;
    cam_switch_ack_latched = 0U;
    return 1U;
}

/**
 * @brief  非阻塞发送目标切换状态和请求 ID。
 * @param  switch_state  0 表示空闲，1 表示请求切换。
 * @param  request_id    非零请求 ID。
 * @return 消息入队结果。
 */
cam_status_t cam_send_switch_command(uint8_t switch_state, uint8_t request_id)
{
    uint8_t payload[CAM_SWITCH_COMMAND_PAYLOAD_SIZE];

    if ((switch_state > 1U) || (request_id == 0U)) {
        return CAM_STATUS_INVALID_ARGUMENT;
    }

    payload[0] = switch_state;
    payload[1] = request_id;
    return cam_send_message(
        CAM_MESSAGE_SWITCH_COMMAND,
        payload,
        CAM_SWITCH_COMMAND_PAYLOAD_SIZE);
}

/**
 * @brief  获取摄像头驱动最近一次状态。
 * @return 最近一次 UART 或队列状态。
 */
cam_status_t cam_get_last_status(void)
{
    if (cam_instance.huart == NULL) {
        return CAM_STATUS_NOT_INITIALIZED;
    }
    return cam_instance.last_status;
}

/**
 * @brief  获取发送队列中的帧数量。
 * @return 包含当前 DMA 帧的待完成帧数。
 */
uint8_t cam_get_pending_tx_count(void)
{
    return cam_instance.tx_count;
}

/**
 * @brief  处理 USART DMA Receive-to-Idle 事件并重新启动接收。
 * @param  huart  触发事件的 UART 句柄。
 * @param  size   DMA 缓冲区中的有效字节数。
 * @return 无。
 */
void cam_rx_callback(UART_HandleTypeDef *huart, uint16_t size)
{
    HAL_StatusTypeDef hal_status;
    uint16_t next_write_pos;
    uint16_t index;

    if ((huart == NULL) || (cam_instance.huart == NULL)
        || (huart->Instance != cam_instance.huart->Instance)) {
        return;
    }
    if (size > CAM_DMA_RX_BUF_SIZE) {
        size = CAM_DMA_RX_BUF_SIZE;
        error_report(ERROR_SOURCE_CAM, DRV_ERR_PARAM);
    }

    for (index = 0U; index < size; index++) {
        next_write_pos =
            (uint16_t)((cam_instance.rx_write_pos + 1U) % CAM_RX_FIFO_SIZE);
        if (next_write_pos == cam_instance.rx_read_pos) {
            error_report(ERROR_SOURCE_CAM, DRV_ERR_BUSY);
            break;
        }
        cam_instance.rx_fifo[cam_instance.rx_write_pos] =
            cam_instance.dma_rx_buffer[index];
        cam_instance.rx_write_pos = next_write_pos;
    }

    memset(cam_instance.dma_rx_buffer, 0, sizeof(cam_instance.dma_rx_buffer));
    hal_status = HAL_UARTEx_ReceiveToIdle_DMA(
        cam_instance.huart,
        cam_instance.dma_rx_buffer,
        CAM_DMA_RX_BUF_SIZE);
    if (hal_status != HAL_OK) {
        cam_instance.last_status = cam_map_hal_status(hal_status);
    }
}

/**
 * @brief  提交已完成的 USART3 DMA 发送帧。
 * @param  huart  触发回调的 UART 句柄。
 * @return 无。
 */
void cam_tx_callback(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (cam_instance.huart == NULL)
        || (huart->Instance != cam_instance.huart->Instance)) {
        return;
    }

    if (cam_instance.tx_count > 0U) {
        cam_instance.tx_read_pos =
            (uint8_t)((cam_instance.tx_read_pos + 1U) % CAM_TX_QUEUE_CAPACITY);
        cam_instance.tx_count--;
    }
    cam_instance.is_tx_busy = 0U;
}

/**
 * @brief  处理 USART3 错误并恢复 DMA 空闲接收。
 * @param  huart  触发回调的 UART 句柄。
 * @return 无。
 */
void cam_error_callback(UART_HandleTypeDef *huart)
{
    HAL_StatusTypeDef hal_status;

    if ((huart == NULL) || (cam_instance.huart == NULL)
        || (huart->Instance != cam_instance.huart->Instance)) {
        return;
    }

    error_report(ERROR_SOURCE_CAM, DRV_ERR_IO);
    cam_instance.is_tx_busy = 0U;
    cam_instance.rx_read_pos = 0U;
    cam_instance.rx_write_pos = 0U;
    cam_instance.logical_frame_length = 0U;
    cam_instance.parse_state = CAM_PARSE_WAIT_HEADER;
    memset(cam_instance.dma_rx_buffer, 0, sizeof(cam_instance.dma_rx_buffer));
    hal_status = HAL_UARTEx_ReceiveToIdle_DMA(
        cam_instance.huart,
        cam_instance.dma_rx_buffer,
        CAM_DMA_RX_BUF_SIZE);
    cam_instance.last_status = cam_map_hal_status(hal_status);
}
