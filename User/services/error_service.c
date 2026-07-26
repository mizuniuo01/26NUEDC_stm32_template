/**
 * @file error_service.c
 * @brief Bounded diagnostic event service implementation.
 */
#include "error_service.h"

#include <string.h>

static error_event_t events[ERROR_SERVICE_EVENT_CAPACITY];
static volatile uint8_t event_count;
static volatile uint8_t event_head;
static volatile uint32_t event_sequence;

void error_service_init(void)
{
    (void)memset(events, 0, sizeof(events));
    event_count = 0U;
    event_head = 0U;
    event_sequence = 0U;
}

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

uint32_t error_service_sequence(void)
{
    return event_sequence;
}

status_code_t error_service_last(status_source_t *source, uint32_t *timestamp_ms)
{
    uint8_t index;

    if (event_count == 0U) {
        return STATUS_UNAVAILABLE;
    }
    index = (uint8_t)((event_head + ERROR_SERVICE_EVENT_CAPACITY - 1U) %
                      ERROR_SERVICE_EVENT_CAPACITY);
    if (source != NULL) {
        *source = events[index].source;
    }
    if (timestamp_ms != NULL) {
        *timestamp_ms = events[index].timestamp_ms;
    }
    return events[index].code;
}

uint8_t error_service_snapshot(error_event_t *snapshot, uint8_t capacity)
{
    uint8_t i;
    uint8_t count;
    uint8_t first;

    if ((snapshot == NULL) || (capacity == 0U)) {
        return 0U;
    }
    count = event_count < capacity ? event_count : capacity;
    first = (uint8_t)((event_head + ERROR_SERVICE_EVENT_CAPACITY - event_count) %
                      ERROR_SERVICE_EVENT_CAPACITY);
    for (i = 0U; i < count; i++) {
        snapshot[i] = events[(uint8_t)((first + i) % ERROR_SERVICE_EVENT_CAPACITY)];
    }
    return count;
}
