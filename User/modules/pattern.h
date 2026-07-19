#ifndef PATTERN_H
#define PATTERN_H

#include <stdint.h>

/* 循迹图案状态 */
typedef enum {
    PATTERN_STRAIGHT = 0,
    PATTERN_SMALL_LEFT,
    PATTERN_SMALL_RIGHT,
    PATTERN_MEDIUM_LEFT,
    PATTERN_MEDIUM_RIGHT,
    PATTERN_BIG_LEFT,
    PATTERN_BIG_RIGHT,
    PATTERN_RIGHT_ANGLE_LEFT,
    PATTERN_RIGHT_ANGLE_RIGHT,
    PATTERN_CROSS,
    PATTERN_UNKNOWN,
    PATTERN_COUNT
} pattern_state_t;

pattern_state_t pattern_lookup(uint8_t sensor_data);

#endif
