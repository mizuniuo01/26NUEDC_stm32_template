/**
 * @file driver_gyro_protocol.c
 * @brief 解析 WIT 姿态传感器的 0x55/0x53 角度数据帧。
 */
#include "driver_gyro_protocol.h"
#include <string.h>

/**
 * @brief  校验一帧 WIT 数据的累加和
 * @param  frame 长度为 11 字节的完整协议帧
 * @retval true 校验和正确
 * @retval false 校验和错误
 */
static bool is_checksum_valid(const uint8_t *frame)
{
    uint8_t sum = 0U;
    uint8_t i;

    for (i = 0U; i < 10U; i++) {
        sum = (uint8_t)(sum + frame[i]);
    }
    return sum == frame[10];
}

/**
 * @brief  从小端字节序列读取一个有符号 16 位整数
 * @param  data 至少包含两个字节的输入地址
 * @return 解码后的有符号整数
 */
static int16_t read_i16(const uint8_t *data)
{
    return (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

/**
 * @brief  初始化 WIT 姿态协议解析器
 * @param  protocol 协议解析器实例，为空时不执行操作
 */
void driver_gyro_protocol_init(driver_gyro_protocol_t *protocol)
{
    if (protocol) {
        (void)memset(protocol, 0, sizeof(*protocol));
    }
}

/**
 * @brief  向 WIT 姿态协议解析器提交一段串口字节流
 * @param  protocol 协议解析器实例
 * @param  data 输入字节序列
 * @param  length 输入字节数
 */
void driver_gyro_protocol_push(driver_gyro_protocol_t *protocol, const uint8_t *data,
    uint16_t length)
{
    uint16_t i;

    if (!protocol || !data) {
        return;
    }
    for (i = 0U; i < length; i++) {
        if ((protocol->index == 0U) && (data[i] != 0x55U)) {
            continue;
        }
        if ((protocol->index == 1U) && (data[i] != 0x53U)) {
            protocol->index = data[i] == 0x55U ? 1U : 0U;
            continue;
        }
        protocol->frame[protocol->index++] = data[i];
        if (protocol->index == sizeof(protocol->frame)) {
            if (is_checksum_valid(protocol->frame)) {
                protocol->attitude.roll = (float)read_i16(&protocol->frame[2]) * 180.0f / 32768.0f;
                protocol->attitude.pitch = (float)read_i16(&protocol->frame[4]) * 180.0f / 32768.0f;
                protocol->attitude.yaw = (float)read_i16(&protocol->frame[6]) * 180.0f / 32768.0f;
                protocol->attitude.is_valid = true;
                protocol->attitude.sequence++;
            }
            protocol->index = 0U;
        }
    }
}

/**
 * @brief  获取最近一次有效姿态角快照
 * @param  protocol 协议解析器实例
 * @param  attitude 接收姿态角数据的存储地址
 * @retval STATUS_OK 有效快照已写入
 * @retval STATUS_INVALID_ARGUMENT 任一参数为空
 * @retval STATUS_UNAVAILABLE 尚未解析到有效角度帧
 */
status_code_t driver_gyro_protocol_snapshot(const driver_gyro_protocol_t *protocol,
    driver_gyro_attitude_t *attitude)
{
    if (!protocol || !attitude) {
        return STATUS_INVALID_ARGUMENT;
    }
    *attitude = protocol->attitude;
    return protocol->attitude.is_valid ? STATUS_OK : STATUS_UNAVAILABLE;
}
