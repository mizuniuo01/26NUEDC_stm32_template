#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

/* 蓝牙屏显行 Y 坐标。 */
typedef enum {
    DISPLAY_LINE_1_Y = 0,    /* 第 1 行。 */
    DISPLAY_LINE_2_Y = 20,   /* 第 2 行。 */
    DISPLAY_LINE_3_Y = 40,   /* 第 3 行。 */
    DISPLAY_LINE_4_Y = 60,   /* 第 4 行。 */
    DISPLAY_LINE_5_Y = 80,   /* 第 5 行。 */
    DISPLAY_LINE_6_Y = 100,  /* 第 6 行。 */
    DISPLAY_LINE_7_Y = 120,  /* 第 7 行。 */
    DISPLAY_LINE_8_Y = 140,  /* 第 8 行。 */
    DISPLAY_LINE_9_Y = 160,  /* 第 9 行。 */
    DISPLAY_LINE_10_Y = 180, /* 第 10 行。 */
} display_line_y_t;

/* TIM6 中断置位、主循环消费的蓝牙显示刷新标志。 */
extern volatile uint8_t display_refresh_flag;

/* 蓝牙统一数据显示与错误信息接口。 */
void display_task(void);
void display_show_error(const char *format, ...);

/* 自动瞄准调度层更新、显示任务持续输出的距离缓存。 */
void display_set_distance(float distance_mm);
void display_clear_distance(void);

#endif /* DISPLAY_H */
