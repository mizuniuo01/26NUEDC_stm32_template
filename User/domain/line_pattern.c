/**
 * @file line_pattern.c
 * @brief 通过查表将巡线传感器位掩码解释为线路形态。
 */
#include "line_pattern.h"

#include <stddef.h>

/* 传感器位掩码与线路形态的单项映射 */
typedef struct {
    uint8_t mask;             /* 需要精确匹配的传感器位掩码 */
    line_pattern_kind_t kind; /* 该位掩码对应的线路形态 */
    int16_t error;            /* 左负右正的离散偏差 */
} line_pattern_entry_t;

/* 构造一项使用指定成员的查表记录 */
#define LINE_PATTERN_ENTRY(pattern_mask, pattern_kind, pattern_error) \
    {                                                               \
        .mask = (pattern_mask),                                      \
        .kind = (pattern_kind),                                      \
        .error = (pattern_error),                                    \
    }

/* 原巡线策略的完整匹配表，bit7 最左、bit0 最右 */
static const line_pattern_entry_t line_pattern_table[] = {
    LINE_PATTERN_ENTRY(0x18U, LINE_PATTERN_STRAIGHT, 0),
    LINE_PATTERN_ENTRY(0x08U, LINE_PATTERN_STRAIGHT, 0),
    LINE_PATTERN_ENTRY(0x10U, LINE_PATTERN_STRAIGHT, 0),
    LINE_PATTERN_ENTRY(0x99U, LINE_PATTERN_STRAIGHT, 0),
    LINE_PATTERN_ENTRY(0x19U, LINE_PATTERN_STRAIGHT, 0),
    LINE_PATTERN_ENTRY(0x98U, LINE_PATTERN_STRAIGHT, 0),
    LINE_PATTERN_ENTRY(0x88U, LINE_PATTERN_STRAIGHT, 0),
    LINE_PATTERN_ENTRY(0x09U, LINE_PATTERN_STRAIGHT, 0),
    LINE_PATTERN_ENTRY(0x11U, LINE_PATTERN_STRAIGHT, 0),
    LINE_PATTERN_ENTRY(0x89U, LINE_PATTERN_STRAIGHT, 0),
    LINE_PATTERN_ENTRY(0xD8U, LINE_PATTERN_STRAIGHT, 0),
    LINE_PATTERN_ENTRY(0x78U, LINE_PATTERN_STRAIGHT, 0),
    LINE_PATTERN_ENTRY(0x38U, LINE_PATTERN_STRAIGHT, 0),
    LINE_PATTERN_ENTRY(0x1BU, LINE_PATTERN_STRAIGHT, 0),
    LINE_PATTERN_ENTRY(0x1EU, LINE_PATTERN_STRAIGHT, 0),
    LINE_PATTERN_ENTRY(0x1CU, LINE_PATTERN_STRAIGHT, 0),
    LINE_PATTERN_ENTRY(0x04U, LINE_PATTERN_SMALL_LEFT, -1),
    LINE_PATTERN_ENTRY(0x20U, LINE_PATTERN_SMALL_RIGHT, 1),
    LINE_PATTERN_ENTRY(0x02U, LINE_PATTERN_MEDIUM_LEFT, -2),
    LINE_PATTERN_ENTRY(0x06U, LINE_PATTERN_MEDIUM_LEFT, -2),
    LINE_PATTERN_ENTRY(0x40U, LINE_PATTERN_MEDIUM_RIGHT, 2),
    LINE_PATTERN_ENTRY(0x60U, LINE_PATTERN_MEDIUM_RIGHT, 2),
    LINE_PATTERN_ENTRY(0x01U, LINE_PATTERN_BIG_LEFT, -3),
    LINE_PATTERN_ENTRY(0x03U, LINE_PATTERN_BIG_LEFT, -3),
    LINE_PATTERN_ENTRY(0x80U, LINE_PATTERN_BIG_RIGHT, 3),
    LINE_PATTERN_ENTRY(0xC0U, LINE_PATTERN_BIG_RIGHT, 3),
    LINE_PATTERN_ENTRY(0x0FU, LINE_PATTERN_RIGHT_ANGLE_LEFT, 0),
    LINE_PATTERN_ENTRY(0x1FU, LINE_PATTERN_RIGHT_ANGLE_LEFT, 0),
    LINE_PATTERN_ENTRY(0x07U, LINE_PATTERN_RIGHT_ANGLE_LEFT, 0),
    LINE_PATTERN_ENTRY(0x2FU, LINE_PATTERN_RIGHT_ANGLE_LEFT, 0),
    LINE_PATTERN_ENTRY(0x6FU, LINE_PATTERN_RIGHT_ANGLE_LEFT, 0),
    LINE_PATTERN_ENTRY(0xF0U, LINE_PATTERN_RIGHT_ANGLE_RIGHT, 0),
    LINE_PATTERN_ENTRY(0xF8U, LINE_PATTERN_RIGHT_ANGLE_RIGHT, 0),
    LINE_PATTERN_ENTRY(0xE0U, LINE_PATTERN_RIGHT_ANGLE_RIGHT, 0),
    LINE_PATTERN_ENTRY(0xF4U, LINE_PATTERN_RIGHT_ANGLE_RIGHT, 0),
    LINE_PATTERN_ENTRY(0xF6U, LINE_PATTERN_RIGHT_ANGLE_RIGHT, 0),
    LINE_PATTERN_ENTRY(0xFFU, LINE_PATTERN_CROSS, 0),
    LINE_PATTERN_ENTRY(0x7FU, LINE_PATTERN_CROSS, 0),
    LINE_PATTERN_ENTRY(0x3FU, LINE_PATTERN_CROSS, 0),
    LINE_PATTERN_ENTRY(0xFCU, LINE_PATTERN_CROSS, 0),
    LINE_PATTERN_ENTRY(0xFEU, LINE_PATTERN_CROSS, 0),
};

/**
 * @brief  查表解码一次巡线传感器数据
 * @param  mask 巡线传感器位掩码，bit7 最左、bit0 最右
 * @param  is_valid 输入数据有效标志
 * @return 查表得到的线路形态、离散偏差和原始位掩码
 */
line_pattern_result_t line_pattern_decode(uint8_t mask, bool is_valid)
{
    line_pattern_result_t result = {
        .kind = LINE_PATTERN_UNKNOWN,
        .error = 0,
        .active_mask = mask,
    };
    size_t i;

    if (!is_valid) {
        result.kind = LINE_PATTERN_INVALID;
        return result;
    }

    for (i = 0U; i < (sizeof(line_pattern_table) / sizeof(line_pattern_table[0]));
         i++) {
        if (line_pattern_table[i].mask == mask) {
            result.kind = line_pattern_table[i].kind;
            result.error = line_pattern_table[i].error;
            break;
        }
    }

    return result;
}
