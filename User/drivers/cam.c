/**
 * @file    cam.c
 * @brief   摄像头串口双向通信模块。
 * @author  mizuniuo01
 * @date    2026-07-21
 * @version 2.0.0
 * @note    USART3 使用 DMA Receive-to-Idle 接收和 DMA 队列发送。
 * @note    接收协议为 0xFF + ASCII(error_x,error_y,ack) + 0xFE。
 * @note    无目标时 payload 为 "-,-,ack"，ack=1 表示目标切换成功。
 * @note    cam_send_packet() 自动封包，cam_send_raw() 原样发送字节流。
 * @warning HAL 回调中只搬运数据或提交队首帧，不启动下一次 DMA 发送。
 *
 * @usage
 * uint8_t command[] = {0x01U, 0x02U, 0xFEU};
 * cam_send_packet(command, sizeof(command));
 *
 * while (1) {
 *     cam_task();
 * }
 */

#include "cam.h"
#include "error_handler.h"
#include <string.h>

/** @brief 摄像头通信模块的单实例状态。 */
static cam_handle_t cam_instance;

/** @brief 最近一次有效的摄像头解析数据。 */
static cam_data_t cam_data;

/** @brief 目标切换成功 ACK 锁存标志，读取后清零。 */
static uint8_t cam_switch_ack_latched;

/** @brief 完整接收帧就绪标志。 */
volatile uint8_t cam_frame_ready;

/**
 * @brief  将 HAL 状态转换为 cam 状态并按需上报错误。
 * @param  hal_status  HAL UART 操作状态。
 * @return 对应的 cam 状态。
 */
static cam_status_t cam_handle_hal_status(HAL_StatusTypeDef hal_status)
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
 * @brief  发布已经写入当前发送槽位的帧。
 * @param  length  当前发送槽位中的有效字节数。
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
 * @brief  获取当前可写发送槽位。
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
 * @brief  在串口空闲时启动队首帧的 DMA 发送。
 * @return 无。
 */
static void cam_start_next_transmission(void)
{
    cam_tx_frame_t *frame;
    HAL_StatusTypeDef hal_status;

    if ((cam_instance.huart == NULL) || cam_instance.is_tx_busy
        || (cam_instance.tx_count == 0U)) {
        return;
    }

    frame = &cam_instance.tx_queue[cam_instance.tx_read_pos];
    hal_status = HAL_UART_Transmit_DMA(cam_instance.huart, frame->data, frame->length);
    cam_instance.last_status = cam_handle_hal_status(hal_status);
    if (hal_status == HAL_OK) {
        cam_instance.is_tx_busy = 1U;
    }
}

/**
 * @brief  将一个 ASCII 有符号十进数字段转换为 int16_t。
 * @param  data    字段首地址。
 * @param  length  字段长度。
 * @param  value   转换结果输出地址。
 * @return 格式和范围有效返回 1，否则返回 0。
 */
static uint8_t cam_parse_int16(const uint8_t *data, uint16_t length, int16_t *value)
{
    uint32_t digit;
    uint32_t magnitude;
    uint32_t limit;
    uint16_t index;
    uint8_t is_negative;

    if ((data == NULL) || (value == NULL) || (length == 0U)) {
        return 0U;
    }

    index = 0U;
    is_negative = 0U;
    if (data[index] == '-') {
        is_negative = 1U;
        index++;
    } else if (data[index] == '+') {
        index++;
    }
    if (index >= length) {
        return 0U;
    }

    magnitude = 0U;
    limit = is_negative ? 32768U : 32767U;
    while (index < length) {
        if ((data[index] < '0') || (data[index] > '9')) {
            return 0U;
        }
        digit = (uint32_t)(data[index] - '0');
        if (magnitude > ((limit - digit) / 10U)) {
            return 0U;
        }
        magnitude = (magnitude * 10U) + digit;
        index++;
    }

    if (is_negative) {
        *value = (int16_t)(-(int32_t)magnitude);
    } else {
        *value = (int16_t)magnitude;
    }
    return 1U;
}

/**
 * @brief  解析 MaixCAM 的 ASCII 三字段 payload。
 * @param  payload  不含帧头帧尾的 payload。
 * @param  length   payload 长度。
 * @param  data     解析结果输出地址。
 * @return 解析成功返回 1，否则返回 0。
 * @note   支持 "x,y,ack" 和无目标格式 "-,-,ack"。
 */
static uint8_t cam_parse_payload(
    const uint8_t *payload,
    uint16_t length,
    cam_data_t *data)
{
    uint16_t first_comma;
    uint16_t second_comma;
    uint16_t index;
    uint8_t first_is_empty_target;
    uint8_t second_is_empty_target;

    if ((payload == NULL) || (data == NULL) || (length < 5U)) {
        return 0U;
    }

    first_comma = length;
    second_comma = length;
    for (index = 0U; index < length; index++) {
        if (payload[index] != ',') {
            continue;
        }
        if (first_comma == length) {
            first_comma = index;
        } else if (second_comma == length) {
            second_comma = index;
        } else {
            return 0U;
        }
    }

    if ((first_comma == 0U) || (second_comma <= (first_comma + 1U))
        || (second_comma >= (length - 1U)) || ((length - second_comma) != 2U)) {
        return 0U;
    }
    if ((payload[second_comma + 1U] != '0') && (payload[second_comma + 1U] != '1')) {
        return 0U;
    }

    first_is_empty_target =
        ((first_comma == 1U) && (payload[0] == '-')) ? 1U : 0U;
    second_is_empty_target =
        (((second_comma - first_comma) == 2U) && (payload[first_comma + 1U] == '-'))
            ? 1U
            : 0U;
    if (first_is_empty_target || second_is_empty_target) {
        if (!first_is_empty_target || !second_is_empty_target) {
            return 0U;
        }
        data->error_x = 0;
        data->error_y = 0;
        data->has_target = 0U;
    } else {
        if (!cam_parse_int16(payload, first_comma, &data->error_x)
            || !cam_parse_int16(
                &payload[first_comma + 1U],
                second_comma - first_comma - 1U,
                &data->error_y)) {
            return 0U;
        }
        data->has_target = 1U;
    }

    data->switch_ack = (uint8_t)(payload[second_comma + 1U] - '0');
    return 1U;
}

/**
 * @brief  解析接收 FIFO 中的全部摄像头协议字节。
 * @return 无。
 */
static void cam_parse_received_frames(void)
{
    cam_data_t parsed_data;
    uint8_t byte;

    while (cam_instance.rx_read_pos != cam_instance.rx_write_pos) {
        byte = cam_instance.rx_fifo[cam_instance.rx_read_pos];
        cam_instance.rx_read_pos =
            (cam_instance.rx_read_pos + 1U) % CAM_RX_FIFO_SIZE;

        switch (cam_instance.rx_state) {
            case CAM_STATE_WAIT_HEADER:
                if (byte == CAM_FRAME_HEADER) {
                    cam_instance.frame_index = 0U;
                    cam_instance.rx_state = CAM_STATE_RECEIVING_DATA;
                }
                break;

            case CAM_STATE_RECEIVING_DATA:
                if (byte == CAM_FRAME_TAIL) {
                    if (cam_parse_payload(
                            cam_instance.frame_buffer,
                            cam_instance.frame_index,
                            &parsed_data)) {
                        cam_data = parsed_data;
                        if (parsed_data.switch_ack) {
                            cam_switch_ack_latched = 1U;
                        }
                        cam_frame_ready = 1U;
                    }
                    cam_instance.rx_state = CAM_STATE_WAIT_HEADER;
                } else if (byte == CAM_FRAME_HEADER) {
                    cam_instance.frame_index = 0U;
                } else if (byte == CAM_ESC_BYTE) {
                    cam_instance.rx_state = CAM_STATE_ESCAPE;
                } else if (cam_instance.frame_index < CAM_MAX_FRAME_LEN) {
                    cam_instance.frame_buffer[cam_instance.frame_index++] = byte;
                } else {
                    cam_instance.rx_state = CAM_STATE_WAIT_HEADER;
                }
                break;

            case CAM_STATE_ESCAPE:
                if (byte == CAM_ESC_TAIL) {
                    byte = CAM_FRAME_TAIL;
                } else if (byte == CAM_ESC_HEADER) {
                    byte = CAM_FRAME_HEADER;
                } else if (byte == CAM_ESC_ESC) {
                    byte = CAM_ESC_BYTE;
                }

                if (cam_instance.frame_index < CAM_MAX_FRAME_LEN) {
                    cam_instance.frame_buffer[cam_instance.frame_index++] = byte;
                    cam_instance.rx_state = CAM_STATE_RECEIVING_DATA;
                } else {
                    cam_instance.rx_state = CAM_STATE_WAIT_HEADER;
                }
                break;

            default:
                cam_instance.rx_state = CAM_STATE_WAIT_HEADER;
                break;
        }
    }
}

/**
 * @brief  初始化摄像头串口通信并启动 DMA 空闲接收。
 * @param  huart  已配置 USART3 RX/TX DMA 的 HAL 串口句柄。
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
    cam_instance.rx_state = CAM_STATE_WAIT_HEADER;
    cam_switch_ack_latched = 0U;
    cam_frame_ready = 0U;

    hal_status = HAL_UARTEx_ReceiveToIdle_DMA(
        huart,
        cam_instance.dma_rx_buffer,
        CAM_DMA_RX_BUF_SIZE);
    cam_instance.last_status = cam_handle_hal_status(hal_status);
}

/**
 * @brief  将自定义 payload 封装为摄像头协议帧并加入 DMA 发送队列。
 * @param  payload  自定义 payload；length 为 0 时允许为 NULL。
 * @param  length   payload 长度，范围为 0～128 字节。
 * @return 入队成功返回 CAM_STATUS_OK，否则返回参数、状态或队列错误。
 */
cam_status_t cam_send_packet(const uint8_t *payload, uint16_t length)
{
    cam_tx_frame_t *frame;
    uint16_t source_index;
    uint16_t frame_index;
    uint8_t byte;

    if (((payload == NULL) && (length > 0U))
        || (length > CAM_TX_PACKET_MAX_PAYLOAD_LEN)) {
        cam_instance.last_status = CAM_STATUS_INVALID_ARGUMENT;
        error_report(ERROR_SOURCE_CAM, DRV_ERR_PARAM);
        return CAM_STATUS_INVALID_ARGUMENT;
    }

    frame = cam_get_tx_write_frame();
    if (frame == NULL) {
        return cam_instance.last_status;
    }

    frame_index = 0U;
    frame->data[frame_index++] = CAM_FRAME_HEADER;
    for (source_index = 0U; source_index < length; source_index++) {
        byte = payload[source_index];
        if (byte == CAM_FRAME_TAIL) {
            frame->data[frame_index++] = CAM_ESC_BYTE;
            frame->data[frame_index++] = CAM_ESC_TAIL;
        } else if (byte == CAM_FRAME_HEADER) {
            frame->data[frame_index++] = CAM_ESC_BYTE;
            frame->data[frame_index++] = CAM_ESC_HEADER;
        } else if (byte == CAM_ESC_BYTE) {
            frame->data[frame_index++] = CAM_ESC_BYTE;
            frame->data[frame_index++] = CAM_ESC_ESC;
        } else {
            frame->data[frame_index++] = byte;
        }
    }
    frame->data[frame_index++] = CAM_FRAME_TAIL;

    cam_publish_tx_frame(frame_index);
    return CAM_STATUS_OK;
}

/**
 * @brief  将原始字节流不经封包直接加入 DMA 发送队列。
 * @param  data    原始字节流。
 * @param  length  字节数，范围为 1～128。
 * @return 入队成功返回 CAM_STATUS_OK，否则返回参数、状态或队列错误。
 */
cam_status_t cam_send_raw(const uint8_t *data, uint16_t length)
{
    cam_tx_frame_t *frame;

    if ((data == NULL) || (length == 0U) || (length > CAM_TX_RAW_MAX_LEN)) {
        cam_instance.last_status = CAM_STATUS_INVALID_ARGUMENT;
        error_report(ERROR_SOURCE_CAM, DRV_ERR_PARAM);
        return CAM_STATUS_INVALID_ARGUMENT;
    }

    frame = cam_get_tx_write_frame();
    if (frame == NULL) {
        return cam_instance.last_status;
    }

    memcpy(frame->data, data, length);
    cam_publish_tx_frame(length);
    return CAM_STATUS_OK;
}

/**
 * @brief  获取摄像头通信最近一次状态。
 * @return 最近一次初始化、入队或 DMA 操作状态。
 */
cam_status_t cam_get_last_status(void)
{
    if (cam_instance.huart == NULL) {
        return CAM_STATUS_NOT_INITIALIZED;
    }
    return cam_instance.last_status;
}

/**
 * @brief  获取发送队列中尚未完成的帧数量。
 * @return 包含当前 DMA 帧在内的待完成帧数。
 */
uint8_t cam_get_pending_tx_count(void)
{
    return cam_instance.tx_count;
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
 * @brief  获取摄像头最近一次有效解析数据。
 * @return 摄像头数据快照。
 */
cam_data_t cam_get_data(void)
{
    return cam_data;
}

/**
 * @brief  读取并清除目标切换成功 ACK 锁存事件。
 * @return 自上次读取后收到过 ACK=1 返回 1，否则返回 0。
 */
uint8_t cam_take_switch_ack(void)
{
    uint8_t switch_ack;

    switch_ack = cam_switch_ack_latched;
    cam_switch_ack_latched = 0U;
    return switch_ack;
}

/**
 * @brief  处理 USART DMA Receive-to-Idle 事件并重新启动接收。
 * @param  huart  触发回调的 HAL 串口句柄。
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
        next_write_pos = (cam_instance.rx_write_pos + 1U) % CAM_RX_FIFO_SIZE;
        if (next_write_pos == cam_instance.rx_read_pos) {
            error_report(ERROR_SOURCE_CAM, DRV_ERR_BUSY);
            break;
        }
        cam_instance.rx_fifo[cam_instance.rx_write_pos] = cam_instance.dma_rx_buffer[index];
        cam_instance.rx_write_pos = next_write_pos;
    }

    memset(cam_instance.dma_rx_buffer, 0, sizeof(cam_instance.dma_rx_buffer));
    hal_status = HAL_UARTEx_ReceiveToIdle_DMA(
        cam_instance.huart,
        cam_instance.dma_rx_buffer,
        CAM_DMA_RX_BUF_SIZE);
    if (hal_status != HAL_OK) {
        cam_instance.last_status = cam_handle_hal_status(hal_status);
    }
}

/**
 * @brief  提交 USART DMA 已完成的发送帧。
 * @param  huart  触发回调的 HAL 串口句柄。
 * @return 无。
 * @note   本函数仅由 HAL_UART_TxCpltCallback() 调用，不启动下一帧 DMA。
 */
void cam_tx_callback(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (cam_instance.huart == NULL)
        || (huart->Instance != cam_instance.huart->Instance)
        || !cam_instance.is_tx_busy || (cam_instance.tx_count == 0U)) {
        return;
    }

    cam_instance.tx_read_pos =
        (uint8_t)((cam_instance.tx_read_pos + 1U) % CAM_TX_QUEUE_CAPACITY);
    cam_instance.tx_count--;
    cam_instance.is_tx_busy = 0U;
    cam_instance.last_status = CAM_STATUS_OK;
}

/**
 * @brief  处理 USART 错误并恢复 DMA 空闲接收。
 * @param  huart  触发回调的 HAL 串口句柄。
 * @return 无。
 * @note   未确认完成的队首发送帧保留，cam_task() 将重新尝试发送。
 */
void cam_error_callback(UART_HandleTypeDef *huart)
{
    HAL_StatusTypeDef hal_status;

    if ((huart == NULL) || (cam_instance.huart == NULL)
        || (huart->Instance != cam_instance.huart->Instance)) {
        return;
    }

    cam_instance.rx_read_pos = 0U;
    cam_instance.rx_write_pos = 0U;
    cam_instance.rx_state = CAM_STATE_WAIT_HEADER;
    cam_instance.frame_index = 0U;
    cam_instance.is_tx_busy = 0U;
    cam_instance.last_status = CAM_STATUS_IO_ERROR;
    error_report(ERROR_SOURCE_CAM, DRV_ERR_IO);

    memset(cam_instance.dma_rx_buffer, 0, sizeof(cam_instance.dma_rx_buffer));
    hal_status = HAL_UARTEx_ReceiveToIdle_DMA(
        cam_instance.huart,
        cam_instance.dma_rx_buffer,
        CAM_DMA_RX_BUF_SIZE);
    if (hal_status != HAL_OK) {
        cam_instance.last_status = cam_handle_hal_status(hal_status);
    }
}
