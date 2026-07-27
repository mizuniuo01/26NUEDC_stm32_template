/**
 * @file error_service.c
 * @brief 使用固定容量环形缓冲区保存板级诊断事件。
 */
#include "error_service.h"
#include <string.h>

static error_event_t events[ERROR_SERVICE_EVENT_CAPACITY];
static uint8_t event_count;
static uint8_t event_head;
static uint32_t event_sequence;

/** @brief 清空诊断事件缓冲区并复位事件序号。 */
void error_service_init(void)
{
    (void)memset(events, 0, sizeof(events));
    event_count = 0U;
    event_head = 0U;
    event_sequence = 0U;
}

/**
 * @brief  向环形缓冲区记录一条诊断事件
 * @param  source 事件来源模块
 * @param  code 模块报告的状态码
 * @param  timestamp_ms 事件发生时间，单位：毫秒
 */
void error_service_record(status_source_t source, status_code_t code, uint32_t timestamp_ms)
{
    uint8_t index = event_head;

    events[index].source = source;
    events[index].code = code;
    events[index].timestamp_ms = timestamp_ms;
    events[index].sequence = ++event_sequence;
    event_head = (uint8_t)((event_head + 1U) % ERROR_SERVICE_EVENT_CAPACITY);
    if (event_count < ERROR_SERVICE_EVENT_CAPACITY) {
        event_count++;
    }
}

/**
 * @brief  读取最近一次诊断事件的全局序号
 * @return 已记录事件的累计序号，尚无事件时返回零
 */
uint32_t error_service_sequence(void)
{
    return event_sequence;
}

/**
 * @brief  获取最近一次诊断事件的来源、时间和状态码
 * @param  source 可选的事件来源输出地址
 * @param  timestamp_ms 可选的事件时间输出地址，单位：毫秒
 * @retval STATUS_OK 最近事件报告操作成功
 * @retval STATUS_INVALID_ARGUMENT 最近事件报告参数非法
 * @retval STATUS_NOT_INITIALIZED 最近事件报告模块尚未初始化
 * @retval STATUS_BUSY 最近事件报告模块正忙
 * @retval STATUS_TIMEOUT 最近事件报告操作超时
 * @retval STATUS_IO_ERROR 最近事件报告外设或传输错误
 * @retval STATUS_UNAVAILABLE 尚无事件或最近事件报告能力不可用
 * @retval STATUS_OUT_OF_RANGE 最近事件报告数值超出范围
 * @retval STATUS_STATE_ERROR 最近事件报告当前状态不允许操作
 */
status_code_t error_service_last(status_source_t *source, uint32_t *timestamp_ms)
{
    uint8_t index;

    if (event_count == 0U) {
        return STATUS_UNAVAILABLE;
    }
    index =
        (uint8_t)((event_head + ERROR_SERVICE_EVENT_CAPACITY - 1U) % ERROR_SERVICE_EVENT_CAPACITY);
    if (source) {
        *source = events[index].source;
    }
    if (timestamp_ms) {
        *timestamp_ms = events[index].timestamp_ms;
    }
    return events[index].code;
}

/**
 * @brief  按时间顺序复制当前保存的诊断事件
 * @param  output_events 接收事件副本的数组
 * @param  capacity output_events 可容纳的事件数量
 * @return 实际复制的事件数量，参数无效时返回零
 */
uint8_t error_service_snapshot(error_event_t *output_events, uint8_t capacity)
{
    uint8_t i;
    uint8_t count;
    uint8_t first;

    if (!output_events || (capacity == 0U)) {
        return 0U;
    }
    count = event_count < capacity ? event_count : capacity;
    first = (uint8_t)((event_head + ERROR_SERVICE_EVENT_CAPACITY - event_count) %
                      ERROR_SERVICE_EVENT_CAPACITY);
    for (i = 0U; i < count; i++) {
        output_events[i] = events[(uint8_t)((first + i) % ERROR_SERVICE_EVENT_CAPACITY)];
    }
    return count;
}
