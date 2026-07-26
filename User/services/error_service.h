/**
 * @file error_service.h
 * @brief Bounded diagnostic event service.
 */
#ifndef USER_SERVICES_ERROR_SERVICE_H
#define USER_SERVICES_ERROR_SERVICE_H

#include "status.h"

#define ERROR_SERVICE_EVENT_CAPACITY 16U

typedef struct {
    status_source_t source;
    status_code_t code;
    uint32_t timestamp_ms;
    uint32_t sequence;
} error_event_t;

void error_service_init(void);
void error_service_record(status_source_t source, status_code_t code, uint32_t timestamp_ms);
uint32_t error_service_sequence(void);
status_code_t error_service_last(status_source_t *source, uint32_t *timestamp_ms);
uint8_t error_service_snapshot(error_event_t *events, uint8_t capacity);

#endif
