#ifndef AUTO_BALL_CAR_USER_DOMAIN_LINE_PATTERN_H
#define AUTO_BALL_CAR_USER_DOMAIN_LINE_PATTERN_H /* 头文件保护 */

#include <stdbool.h>
#include <stdint.h>

/* 巡线传感器查表识别出的线路形态 */
typedef enum {
    LINE_PATTERN_STRAIGHT = 0,          /* 中央传感器触发或匹配直线抗噪组合 */
    LINE_PATTERN_SMALL_LEFT,            /* 线路小幅偏左 */
    LINE_PATTERN_SMALL_RIGHT,           /* 线路小幅偏右 */
    LINE_PATTERN_MEDIUM_LEFT,           /* 线路中幅偏左 */
    LINE_PATTERN_MEDIUM_RIGHT,          /* 线路中幅偏右 */
    LINE_PATTERN_BIG_LEFT,              /* 线路大幅偏左 */
    LINE_PATTERN_BIG_RIGHT,             /* 线路大幅偏右 */
    LINE_PATTERN_RIGHT_ANGLE_LEFT,      /* 检测到左侧直角弯 */
    LINE_PATTERN_RIGHT_ANGLE_RIGHT,     /* 检测到右侧直角弯 */
    LINE_PATTERN_CROSS,                 /* 检测到十字或起止线 */
    LINE_PATTERN_UNKNOWN,               /* 有效输入未匹配任何已知组合 */
    LINE_PATTERN_INVALID,               /* 传感器数据无效 */
    LINE_PATTERN_COUNT,                 /* 线路形态枚举成员数 */
} line_pattern_kind_t;

/* 一次线路形态查表解码结果 */
typedef struct {
    line_pattern_kind_t kind; /* 识别出的线路形态 */
    int16_t error;            /* 离散偏差：左负右正，幅值为 0～3 */
    uint8_t active_mask;      /* 原始传感器位掩码，bit7 最左、bit0 最右 */
} line_pattern_result_t;

/* 线路形态查表解码接口 */
line_pattern_result_t line_pattern_decode(uint8_t mask, bool is_valid);

#endif /* AUTO_BALL_CAR_USER_DOMAIN_LINE_PATTERN_H */
