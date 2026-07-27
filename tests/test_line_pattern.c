/**
 * @file test_line_pattern.c
 * @brief 主机侧验证巡线传感器的完整查表映射。
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "line_pattern.h"

typedef struct {
    uint8_t mask;
    line_pattern_kind_t kind;
    int16_t error;
} expected_pattern_t;

#define EXPECTED_PATTERN(pattern_mask, pattern_kind, pattern_error) \
    {                                                             \
        .mask = (pattern_mask),                                    \
        .kind = (pattern_kind),                                    \
        .error = (pattern_error),                                  \
    }

static const expected_pattern_t expected_patterns[] = {
    EXPECTED_PATTERN(0x18U, LINE_PATTERN_STRAIGHT, 0),
    EXPECTED_PATTERN(0x08U, LINE_PATTERN_STRAIGHT, 0),
    EXPECTED_PATTERN(0x10U, LINE_PATTERN_STRAIGHT, 0),
    EXPECTED_PATTERN(0x99U, LINE_PATTERN_STRAIGHT, 0),
    EXPECTED_PATTERN(0x19U, LINE_PATTERN_STRAIGHT, 0),
    EXPECTED_PATTERN(0x98U, LINE_PATTERN_STRAIGHT, 0),
    EXPECTED_PATTERN(0x88U, LINE_PATTERN_STRAIGHT, 0),
    EXPECTED_PATTERN(0x09U, LINE_PATTERN_STRAIGHT, 0),
    EXPECTED_PATTERN(0x11U, LINE_PATTERN_STRAIGHT, 0),
    EXPECTED_PATTERN(0x89U, LINE_PATTERN_STRAIGHT, 0),
    EXPECTED_PATTERN(0xD8U, LINE_PATTERN_STRAIGHT, 0),
    EXPECTED_PATTERN(0x78U, LINE_PATTERN_STRAIGHT, 0),
    EXPECTED_PATTERN(0x38U, LINE_PATTERN_STRAIGHT, 0),
    EXPECTED_PATTERN(0x1BU, LINE_PATTERN_STRAIGHT, 0),
    EXPECTED_PATTERN(0x1EU, LINE_PATTERN_STRAIGHT, 0),
    EXPECTED_PATTERN(0x1CU, LINE_PATTERN_STRAIGHT, 0),
    EXPECTED_PATTERN(0x04U, LINE_PATTERN_SMALL_LEFT, -1),
    EXPECTED_PATTERN(0x20U, LINE_PATTERN_SMALL_RIGHT, 1),
    EXPECTED_PATTERN(0x02U, LINE_PATTERN_MEDIUM_LEFT, -2),
    EXPECTED_PATTERN(0x06U, LINE_PATTERN_MEDIUM_LEFT, -2),
    EXPECTED_PATTERN(0x40U, LINE_PATTERN_MEDIUM_RIGHT, 2),
    EXPECTED_PATTERN(0x60U, LINE_PATTERN_MEDIUM_RIGHT, 2),
    EXPECTED_PATTERN(0x01U, LINE_PATTERN_BIG_LEFT, -3),
    EXPECTED_PATTERN(0x03U, LINE_PATTERN_BIG_LEFT, -3),
    EXPECTED_PATTERN(0x80U, LINE_PATTERN_BIG_RIGHT, 3),
    EXPECTED_PATTERN(0xC0U, LINE_PATTERN_BIG_RIGHT, 3),
    EXPECTED_PATTERN(0x0FU, LINE_PATTERN_RIGHT_ANGLE_LEFT, 0),
    EXPECTED_PATTERN(0x1FU, LINE_PATTERN_RIGHT_ANGLE_LEFT, 0),
    EXPECTED_PATTERN(0x07U, LINE_PATTERN_RIGHT_ANGLE_LEFT, 0),
    EXPECTED_PATTERN(0x2FU, LINE_PATTERN_RIGHT_ANGLE_LEFT, 0),
    EXPECTED_PATTERN(0x6FU, LINE_PATTERN_RIGHT_ANGLE_LEFT, 0),
    EXPECTED_PATTERN(0xF0U, LINE_PATTERN_RIGHT_ANGLE_RIGHT, 0),
    EXPECTED_PATTERN(0xF8U, LINE_PATTERN_RIGHT_ANGLE_RIGHT, 0),
    EXPECTED_PATTERN(0xE0U, LINE_PATTERN_RIGHT_ANGLE_RIGHT, 0),
    EXPECTED_PATTERN(0xF4U, LINE_PATTERN_RIGHT_ANGLE_RIGHT, 0),
    EXPECTED_PATTERN(0xF6U, LINE_PATTERN_RIGHT_ANGLE_RIGHT, 0),
    EXPECTED_PATTERN(0xFFU, LINE_PATTERN_CROSS, 0),
    EXPECTED_PATTERN(0x7FU, LINE_PATTERN_CROSS, 0),
    EXPECTED_PATTERN(0x3FU, LINE_PATTERN_CROSS, 0),
    EXPECTED_PATTERN(0xFCU, LINE_PATTERN_CROSS, 0),
    EXPECTED_PATTERN(0xFEU, LINE_PATTERN_CROSS, 0),
};

static void test_known_patterns(void)
{
    size_t i;

    for (i = 0U; i < (sizeof(expected_patterns) / sizeof(expected_patterns[0])); i++) {
        const line_pattern_result_t result =
            line_pattern_decode(expected_patterns[i].mask, true);

        assert(result.kind == expected_patterns[i].kind);
        assert(result.error == expected_patterns[i].error);
        assert(result.active_mask == expected_patterns[i].mask);
    }
}

static void test_unknown_pattern(void)
{
    uint16_t mask;

    for (mask = 0U; mask <= UINT8_MAX; mask++) {
        bool is_known = false;
        size_t i;

        for (i = 0U; i < (sizeof(expected_patterns) / sizeof(expected_patterns[0]));
             i++) {
            if (expected_patterns[i].mask == (uint8_t)mask) {
                is_known = true;
                break;
            }
        }

        if (!is_known) {
            const line_pattern_result_t result =
                line_pattern_decode((uint8_t)mask, true);

            assert(result.kind == LINE_PATTERN_UNKNOWN);
            assert(result.error == 0);
            assert(result.active_mask == (uint8_t)mask);
        }
    }
}

static void test_invalid_sample(void)
{
    const line_pattern_result_t result = line_pattern_decode(0x18U, false);

    assert(result.kind == LINE_PATTERN_INVALID);
    assert(result.error == 0);
    assert(result.active_mask == 0x18U);
}

int main(void)
{
    test_known_patterns();
    test_unknown_pattern();
    test_invalid_sample();
    return 0;
}
