#ifndef AUTO_BALL_CAR_USER_SERVICES_ERROR_SERVICE_H
#define AUTO_BALL_CAR_USER_SERVICES_ERROR_SERVICE_H /* 头文件保护 */

#include "status.h"
#include <stdint.h>

#define ERROR_SERVICE_EVENT_CAPACITY 16U /* 诊断事件环形缓冲区容量 */

/* 一条带来源和时间戳的诊断事件 */
typedef struct {
    status_source_t source; /* 事件来源模块 */
    status_code_t code;     /* 模块报告的状态码 */
    uint32_t timestamp_ms;  /* 事件发生时间，单位：毫秒 */
    uint32_t sequence;      /* 全局事件序号 */
} error_event_t;

/* 生命周期、事件记录与查询接口 */
void error_service_init(void);
void error_service_record(status_source_t source, status_code_t code, uint32_t timestamp_ms);
uint32_t error_service_sequence(void);
status_code_t error_service_last(status_source_t *source, uint32_t *timestamp_ms);
uint8_t error_service_snapshot(error_event_t *output_events, uint8_t capacity);

#endif /* AUTO_BALL_CAR_USER_SERVICES_ERROR_SERVICE_H */
