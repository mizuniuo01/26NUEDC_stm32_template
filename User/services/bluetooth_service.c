/**
 * @file bluetooth_service.c
 * @brief 适配蓝牙串口小程序协议并调度连续的异步发送。
 */
#include "bluetooth_service.h"
#include "bsp_board.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define BLUETOOTH_SERVICE_TEXT_CAPACITY 96U /* 单条文本内容容量，单位：字节 */
#define BLUETOOTH_SERVICE_FRAME_CAPACITY 128U /* 单条协议帧容量，单位：字节 */
#define BLUETOOTH_SERVICE_TX_QUEUE_CAPACITY 512U /* 待发送队列容量，单位：字节 */
#define BLUETOOTH_SERVICE_TX_CHUNK_SIZE_BYTES 128U /* 单次 DMA 发送字节数 */

static uint8_t tx_queue[BLUETOOTH_SERVICE_TX_QUEUE_CAPACITY];
static uint8_t tx_chunk[BLUETOOTH_SERVICE_TX_CHUNK_SIZE_BYTES];
static uint16_t tx_read_index;
static uint16_t tx_write_index;
static uint16_t tx_count;
static bool is_initialized;

/**
 * @brief  将一段完整数据加入服务发送队列
 * @param  data 待入队数据
 * @param  length 待入队字节数
 * @retval STATUS_OK 数据已加入队列
 * @retval STATUS_INVALID_ARGUMENT data 为空或 length 为零
 * @retval STATUS_NOT_INITIALIZED 服务尚未初始化
 * @retval STATUS_BUSY 队列剩余空间不足
 */
static status_code_t enqueue(const uint8_t *data, uint16_t length)
{
    uint16_t index;

    if (!data || (length == 0U)) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (length > (uint16_t)(BLUETOOTH_SERVICE_TX_QUEUE_CAPACITY - tx_count)) {
        return STATUS_BUSY;
    }
    for (index = 0U; index < length; index++) {
        tx_queue[tx_write_index] = data[index];
        tx_write_index =
            (uint16_t)((tx_write_index + 1U) % BLUETOOTH_SERVICE_TX_QUEUE_CAPACITY);
    }
    tx_count = (uint16_t)(tx_count + length);
    return STATUS_OK;
}

/**
 * @brief  检查格式化结果并将其有效字节加入发送队列
 * @param  buffer 格式化输出缓冲区
 * @param  result snprintf 或 vsnprintf 的返回值
 * @param  capacity buffer 容量，单位：字节
 * @retval STATUS_OK 格式化结果已加入队列
 * @retval STATUS_INVALID_ARGUMENT 格式化失败
 * @retval STATUS_OUT_OF_RANGE 格式化结果超过缓冲区容量
 * @retval STATUS_NOT_INITIALIZED 服务尚未初始化
 * @retval STATUS_BUSY 队列剩余空间不足
 */
static status_code_t enqueue_formatted(const char *buffer, int result, size_t capacity)
{
    if (!buffer || (result < 0) || (capacity == 0U)) {
        return STATUS_INVALID_ARGUMENT;
    }
    if ((size_t)result >= capacity) {
        return STATUS_OUT_OF_RANGE;
    }
    if (result == 0) {
        return STATUS_OK;
    }
    return enqueue((const uint8_t *)buffer, (uint16_t)result);
}

/**
 * @brief  初始化蓝牙小程序服务并清空待发送数据
 * @return 无
 */
void bluetooth_service_init(void)
{
    tx_read_index = 0U;
    tx_write_index = 0U;
    tx_count = 0U;
    is_initialized = true;
}

/**
 * @brief  尝试向 BSP 提交一段待发送数据
 * @retval STATUS_OK 队列为空或一段数据已提交发送
 * @retval STATUS_NOT_INITIALIZED 服务或蓝牙 BSP 尚未初始化
 * @retval STATUS_BUSY 蓝牙 BSP 正在发送上一段数据
 * @retval STATUS_IO_ERROR DMA 发送启动失败
 */
status_code_t bluetooth_service_process(void)
{
    status_code_t status;
    uint16_t length;
    uint16_t index;

    if (!is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (tx_count == 0U) {
        return STATUS_OK;
    }
    length = tx_count > BLUETOOTH_SERVICE_TX_CHUNK_SIZE_BYTES
                 ? BLUETOOTH_SERVICE_TX_CHUNK_SIZE_BYTES
                 : tx_count;
    for (index = 0U; index < length; index++) {
        tx_chunk[index] =
            tx_queue[(tx_read_index + index) % BLUETOOTH_SERVICE_TX_QUEUE_CAPACITY];
    }
    status = bsp_bluetooth_write(tx_chunk, length);
    if (status != STATUS_OK) {
        return status;
    }
    tx_read_index =
        (uint16_t)((tx_read_index + length) % BLUETOOTH_SERVICE_TX_QUEUE_CAPACITY);
    tx_count = (uint16_t)(tx_count - length);
    return STATUS_OK;
}

/**
 * @brief  通过蓝牙发送普通格式化文本
 * @param  format printf 格式字符串
 * @param  ... 格式化参数
 * @retval STATUS_OK 文本已加入发送队列
 * @retval STATUS_INVALID_ARGUMENT format 为空或格式化失败
 * @retval STATUS_OUT_OF_RANGE 文本超过单帧容量
 * @retval STATUS_NOT_INITIALIZED 服务尚未初始化
 * @retval STATUS_BUSY 队列剩余空间不足
 */
status_code_t bluetooth_service_printf(const char *format, ...)
{
    char frame[BLUETOOTH_SERVICE_FRAME_CAPACITY];
    va_list arguments;
    int result;

    if (!format) {
        return STATUS_INVALID_ARGUMENT;
    }
    va_start(arguments, format);
    result = vsnprintf(frame, sizeof(frame), format, arguments);
    va_end(arguments);
    return enqueue_formatted(frame, result, sizeof(frame));
}

/**
 * @brief  在蓝牙小程序指定坐标显示格式化文本
 * @param  x 显示 X 坐标
 * @param  y 显示 Y 坐标
 * @param  format printf 格式字符串
 * @param  ... 格式化参数
 * @retval STATUS_OK 显示帧已加入发送队列
 * @retval STATUS_INVALID_ARGUMENT format 为空或格式化失败
 * @retval STATUS_OUT_OF_RANGE 文本或协议帧超过容量
 * @retval STATUS_NOT_INITIALIZED 服务尚未初始化
 * @retval STATUS_BUSY 队列剩余空间不足
 */
status_code_t bluetooth_service_display(int16_t x, int16_t y, const char *format, ...)
{
    char text[BLUETOOTH_SERVICE_TEXT_CAPACITY];
    char frame[BLUETOOTH_SERVICE_FRAME_CAPACITY];
    va_list arguments;
    int result;

    if (!format) {
        return STATUS_INVALID_ARGUMENT;
    }
    va_start(arguments, format);
    result = vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    if ((result < 0) || ((size_t)result >= sizeof(text))) {
        return result < 0 ? STATUS_INVALID_ARGUMENT : STATUS_OUT_OF_RANGE;
    }
    result = snprintf(frame, sizeof(frame), "[display,%d,%d,%s]\r\n", (int)x, (int)y, text);
    return enqueue_formatted(frame, result, sizeof(frame));
}

/**
 * @brief  通知蓝牙小程序清空显示区
 * @retval STATUS_OK 清屏帧已加入发送队列
 * @retval STATUS_NOT_INITIALIZED 服务尚未初始化
 * @retval STATUS_BUSY 队列剩余空间不足
 */
status_code_t bluetooth_service_clear_display(void)
{
    static const uint8_t frame[] = "[display-clear]\r\n";

    return enqueue(frame, (uint16_t)(sizeof(frame) - 1U));
}

/**
 * @brief  向蓝牙小程序波形绘制器发送格式化数据
 * @param  format printf 格式字符串
 * @param  ... 格式化参数
 * @retval STATUS_OK 绘图帧已加入发送队列
 * @retval STATUS_INVALID_ARGUMENT format 为空或格式化失败
 * @retval STATUS_OUT_OF_RANGE 文本或协议帧超过容量
 * @retval STATUS_NOT_INITIALIZED 服务尚未初始化
 * @retval STATUS_BUSY 队列剩余空间不足
 */
status_code_t bluetooth_service_plot(const char *format, ...)
{
    char text[BLUETOOTH_SERVICE_TEXT_CAPACITY];
    char frame[BLUETOOTH_SERVICE_FRAME_CAPACITY];
    va_list arguments;
    int result;

    if (!format) {
        return STATUS_INVALID_ARGUMENT;
    }
    va_start(arguments, format);
    result = vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    if ((result < 0) || ((size_t)result >= sizeof(text))) {
        return result < 0 ? STATUS_INVALID_ARGUMENT : STATUS_OUT_OF_RANGE;
    }
    result = snprintf(frame, sizeof(frame), "[plot,%s]\r\n", text);
    return enqueue_formatted(frame, result, sizeof(frame));
}
