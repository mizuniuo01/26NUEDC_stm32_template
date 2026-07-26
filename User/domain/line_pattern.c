/**
 * @file line_pattern.c
 * @brief Pure line-sensor interpretation domain logic implementation.
 */
#include "line_pattern.h"

line_pattern_result_t line_pattern_decode(uint8_t mask, uint8_t valid)
{
    line_pattern_result_t result = {LINE_PATTERN_NONE, 0, mask};
    uint8_t count = 0U;
    uint8_t i;
    int16_t weighted = 0;
    static const int8_t weights[8] = {-7, -5, -3, -1, 1, 3, 5, 7};

    if (valid == 0U) {
        result.kind = LINE_PATTERN_INVALID;
        return result;
    }
    for (i = 0U; i < 8U; i++) {
        if ((mask & (uint8_t)(1U << i)) != 0U) {
            count++;
            weighted = (int16_t)(weighted + weights[i]);
        }
    }
    if (count == 0U) {
        result.kind = LINE_PATTERN_NONE;
    } else if (count >= 6U) {
        result.kind = LINE_PATTERN_WIDE;
        result.error = (int16_t)(weighted / count);
    } else {
        result.error = (int16_t)(weighted / count);
        result.kind = result.error < -2 ? LINE_PATTERN_LEFT :
                      (result.error > 2 ? LINE_PATTERN_RIGHT : LINE_PATTERN_CENTER);
    }
    return result;
}
