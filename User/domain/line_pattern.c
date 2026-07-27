/**
 * @file line_pattern.c
 * @brief 将巡线传感器位掩码解释为线路形态和离散偏差。
 */
#include "line_pattern.h"

/**
 * @brief  解码一次巡线传感器数据
 * @param  mask 巡线传感器有效位掩码
 * @param  is_valid 输入数据有效标志
 * @return 线路形态、离散偏差和原始有效位掩码
 */
line_pattern_result_t line_pattern_decode(uint8_t mask, bool is_valid)
{
    line_pattern_result_t result = {
        .kind = LINE_PATTERN_NONE,
        .error = 0,
        .active_mask = mask,
    };
    uint8_t count = 0U;
    uint8_t i;
    int16_t weighted = 0;
    static const int8_t weights[8] = {
        -7,
        -5,
        -3,
        -1,
        1,
        3,
        5,
        7,
    };

    if (!is_valid) {
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
        result.kind = result.error < -2
                          ? LINE_PATTERN_LEFT
                          : (result.error > 2 ? LINE_PATTERN_RIGHT : LINE_PATTERN_CENTER);
    }
    return result;
}
