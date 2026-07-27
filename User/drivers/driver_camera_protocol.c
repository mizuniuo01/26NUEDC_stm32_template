/**
 * @file driver_camera_protocol.c
 * @brief 解析和编码 MaixCAM 转义二进制协议帧。
 */
#include "driver_camera_protocol.h"
#include <limits.h>
#include <string.h>

_Static_assert(CHAR_BIT == 8, "protocol drivers require 8-bit bytes");

enum {
    CAMERA_WAIT_HEADER = 0,       /* 等待帧头状态 */
    CAMERA_RECEIVING = 1,         /* 接收逻辑帧内容状态 */
    CAMERA_ESCAPE = 2,            /* 等待转义后字节状态 */
    CAMERA_HEADER = 0xFF,         /* 物理帧头字节 */
    CAMERA_TAIL = 0xFE,           /* 物理帧尾字节 */
    CAMERA_ESCAPE_BYTE = 0x7D,    /* 转义指示字节 */
    CAMERA_ESCAPE_HEADER = 0x5F,  /* 转义后的帧头值 */
    CAMERA_ESCAPE_TAIL = 0x5E,    /* 转义后的帧尾值 */
    CAMERA_ESCAPE_ESCAPE = 0x5D,  /* 转义后的转义字节值 */
    CAMERA_VERSION = 0x01,        /* 当前协议版本 */
    CAMERA_TARGET_REPORT = 0x01,  /* 视觉目标上报命令 */
    CAMERA_SWITCH_COMMAND = 0x10, /* 视觉处理开关命令 */
};

/**
 * @brief  计算协议使用的 CRC-8 校验值
 * @param  data 待校验字节序列
 * @param  length 待校验字节数
 * @return 使用多项式 0x07 计算的 CRC-8 校验值
 */
static uint8_t crc8(const uint8_t *data, uint16_t length)
{
    uint8_t crc = 0U;
    uint16_t i;
    uint8_t bit;

    for (i = 0U; i < length; i++) {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++) {
            crc = (crc & 0x80U) != 0U ? (uint8_t)((crc << 1U) ^ 0x07U) : (uint8_t)(crc << 1U);
        }
    }
    return crc;
}

/**
 * @brief  校验并解析解析器中已经完成转义的逻辑帧
 * @param  protocol 协议解析器实例
 */
static void process_frame(driver_camera_protocol_t *protocol)
{
    uint8_t payload_length;
    const uint8_t *payload;

    if (protocol->length < 4U) {
        return;
    }
    payload_length = protocol->frame[2];
    if ((protocol->frame[0] != CAMERA_VERSION) ||
        (protocol->length != (uint16_t)(payload_length + 4U)) ||
        (crc8(protocol->frame, (uint16_t)(protocol->length - 1U)) !=
            protocol->frame[protocol->length - 1U])) {
        return;
    }
    if ((protocol->frame[1] != CAMERA_TARGET_REPORT) || (payload_length != 6U)) {
        return;
    }
    payload = &protocol->frame[3];
    if ((payload[0] & (uint8_t)~0x03U) != 0U) {
        return;
    }
    protocol->target.has_target = (payload[0] & 0x01U) != 0U;
    protocol->target.has_switch_ack = ((payload[0] >> 1U) & 0x01U) != 0U;
    protocol->target.switch_ack_id = protocol->target.has_switch_ack ? payload[5] : 0U;
    protocol->target.error_x = protocol->target.has_target
                                   ? (int16_t)((uint16_t)payload[1] | ((uint16_t)payload[2] << 8U))
                                   : 0;
    protocol->target.error_y = protocol->target.has_target
                                   ? (int16_t)((uint16_t)payload[3] | ((uint16_t)payload[4] << 8U))
                                   : 0;
    protocol->target.is_valid = true;
    protocol->target.sequence++;
}

/**
 * @brief  向逻辑帧缓冲区追加一个字节并处理容量溢出
 * @param  protocol 协议解析器实例
 * @param  byte 已完成转义的逻辑字节
 */
static void append(driver_camera_protocol_t *protocol, uint8_t byte)
{
    if (protocol->length < DRIVER_CAMERA_LOGICAL_CAPACITY) {
        protocol->frame[protocol->length++] = byte;
    } else {
        protocol->length = 0U;
        protocol->state = CAMERA_WAIT_HEADER;
    }
}

/**
 * @brief  初始化 MaixCAM 协议解析器
 * @param  protocol 协议解析器实例，为空时不执行操作
 */
void driver_camera_protocol_init(driver_camera_protocol_t *protocol)
{
    if (protocol) {
        (void)memset(protocol, 0, sizeof(*protocol));
    }
}

/**
 * @brief  向协议解析器提交一段串口字节流
 * @param  protocol 协议解析器实例
 * @param  data 输入字节序列
 * @param  length 输入字节数
 */
void driver_camera_protocol_push(driver_camera_protocol_t *protocol, const uint8_t *data,
    uint16_t length)
{
    uint16_t i;
    uint8_t byte;

    if (!protocol || !data) {
        return;
    }
    for (i = 0U; i < length; i++) {
        byte = data[i];
        if (protocol->state == CAMERA_WAIT_HEADER) {
            if (byte == CAMERA_HEADER) {
                protocol->length = 0U;
                protocol->state = CAMERA_RECEIVING;
            }
        } else if (protocol->state == CAMERA_ESCAPE) {
            if (byte == CAMERA_ESCAPE_HEADER) {
                append(protocol, CAMERA_HEADER);
            } else if (byte == CAMERA_ESCAPE_TAIL) {
                append(protocol, CAMERA_TAIL);
            } else if (byte == CAMERA_ESCAPE_ESCAPE) {
                append(protocol, CAMERA_ESCAPE_BYTE);
            } else {
                protocol->state = CAMERA_WAIT_HEADER;
                protocol->length = 0U;
                continue;
            }
            protocol->state = CAMERA_RECEIVING;
        } else if (byte == CAMERA_ESCAPE_BYTE) {
            protocol->state = CAMERA_ESCAPE;
        } else if (byte == CAMERA_TAIL) {
            process_frame(protocol);
            protocol->state = CAMERA_WAIT_HEADER;
            protocol->length = 0U;
        } else if (byte == CAMERA_HEADER) {
            protocol->length = 0U;
        } else {
            append(protocol, byte);
        }
    }
}

/**
 * @brief  获取最近一次有效视觉目标快照
 * @param  protocol 协议解析器实例
 * @param  target 接收视觉目标数据的存储地址
 * @retval STATUS_OK 有效快照已写入
 * @retval STATUS_INVALID_ARGUMENT 任一参数为空
 * @retval STATUS_UNAVAILABLE 尚未解析到有效目标帧
 */
status_code_t driver_camera_protocol_snapshot(const driver_camera_protocol_t *protocol,
    driver_camera_target_t *target)
{
    if (!protocol || !target) {
        return STATUS_INVALID_ARGUMENT;
    }
    *target = protocol->target;
    return protocol->target.is_valid ? STATUS_OK : STATUS_UNAVAILABLE;
}

/**
 * @brief  编码一条包含转义和校验的视觉处理开关命令
 * @param  is_enabled 视觉处理启用标志
 * @param  request_id 用于匹配应答的请求 ID
 * @param  frame 接收物理协议帧的缓冲区
 * @param  capacity frame 的容量，单位：字节
 * @param  length 接收实际编码字节数
 * @retval STATUS_OK 协议帧编码完成
 * @retval STATUS_INVALID_ARGUMENT 输出地址为空或容量小于最短帧
 * @retval STATUS_OUT_OF_RANGE 输出缓冲区不足以容纳转义后的协议帧
 */
status_code_t driver_camera_protocol_encode_switch(bool is_enabled, uint8_t request_id,
    uint8_t *frame, uint16_t capacity, uint16_t *length)
{
    uint8_t logical[6];
    uint8_t i;
    uint16_t output = 0U;

    if (!frame || !length || (capacity < 8U)) {
        return STATUS_INVALID_ARGUMENT;
    }
    logical[0] = CAMERA_VERSION;
    logical[1] = CAMERA_SWITCH_COMMAND;
    logical[2] = 2U;
    logical[3] = is_enabled ? 1U : 0U;
    logical[4] = request_id;
    logical[5] = crc8(logical, 5U);
    frame[output++] = CAMERA_HEADER;
    for (i = 0U; i < sizeof(logical); i++) {
        if ((logical[i] == CAMERA_HEADER) || (logical[i] == CAMERA_TAIL) ||
            (logical[i] == CAMERA_ESCAPE_BYTE)) {
            if ((uint16_t)(output + 2U) > capacity) {
                return STATUS_OUT_OF_RANGE;
            }
            frame[output++] = CAMERA_ESCAPE_BYTE;
            frame[output++] =
                logical[i] == CAMERA_HEADER
                    ? CAMERA_ESCAPE_HEADER
                    : (logical[i] == CAMERA_TAIL ? CAMERA_ESCAPE_TAIL : CAMERA_ESCAPE_ESCAPE);
        } else {
            if ((uint16_t)(output + 1U) > capacity) {
                return STATUS_OUT_OF_RANGE;
            }
            frame[output++] = logical[i];
        }
    }
    frame[output++] = CAMERA_TAIL;
    *length = output;
    return STATUS_OK;
}
