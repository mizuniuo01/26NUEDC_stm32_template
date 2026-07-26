/**
 * @file line_pattern.h
 * @brief Pure line-sensor interpretation domain logic.
 */
#ifndef USER_DOMAIN_LINE_PATTERN_H
#define USER_DOMAIN_LINE_PATTERN_H

#include <stdint.h>

typedef enum {
    LINE_PATTERN_NONE = 0,
    LINE_PATTERN_LEFT = 1,
    LINE_PATTERN_CENTER = 2,
    LINE_PATTERN_RIGHT = 3,
    LINE_PATTERN_WIDE = 4,
    LINE_PATTERN_INVALID = 5
} line_pattern_kind_t;

typedef struct {
    line_pattern_kind_t kind;
    int16_t error;
    uint8_t active_mask;
} line_pattern_result_t;

line_pattern_result_t line_pattern_decode(uint8_t mask, uint8_t valid);

#endif
