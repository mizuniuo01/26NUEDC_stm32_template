#ifndef AUTO_BALL_CAR_USER_DOMAIN_LINE_PATTERN_H
#define AUTO_BALL_CAR_USER_DOMAIN_LINE_PATTERN_H /* 头文件保护 */

#include <stdbool.h>
#include <stdint.h>

/* 巡线传感器识别出的线路形态 */
typedef enum {
    LINE_PATTERN_NONE = 0,    /* 未检测到有效线路 */
    LINE_PATTERN_LEFT = 1,    /* 线路位于车体左侧 */
    LINE_PATTERN_CENTER = 2,  /* 线路位于车体中央 */
    LINE_PATTERN_RIGHT = 3,   /* 线路位于车体右侧 */
    LINE_PATTERN_WIDE = 4,    /* 检测到宽线或交叉区域 */
    LINE_PATTERN_INVALID = 5, /* 传感器数据无效 */
} line_pattern_kind_t;

/* 一次线路形态解码的结果 */
typedef struct {
    line_pattern_kind_t kind; /* 识别出的线路形态 */
    int16_t error;            /* 相对中央位置的离散偏差 */
    uint8_t active_mask;      /* 参与识别的传感器有效位掩码 */
} line_pattern_result_t;

/* 线路形态解码接口 */
line_pattern_result_t line_pattern_decode(uint8_t mask, bool is_valid);

#endif /* AUTO_BALL_CAR_USER_DOMAIN_LINE_PATTERN_H */
