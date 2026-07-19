#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

/* 蓝牙屏显行 Y 坐标 */
typedef enum {
    DISPLAY_LINE_1_Y = 0,    /* 第 1 行 */
    DISPLAY_LINE_2_Y = 20,   /* 第 2 行 */
    DISPLAY_LINE_3_Y = 40,   /* 第 3 行 */
    DISPLAY_LINE_4_Y = 60,   /* 第 4 行 */
    DISPLAY_LINE_5_Y = 80,   /* 第 5 行 */
    DISPLAY_LINE_6_Y = 100,  /* 第 6 行 */
    DISPLAY_LINE_7_Y = 120,  /* 第 7 行 */
    DISPLAY_LINE_8_Y = 140,  /* 第 8 行 */
    DISPLAY_LINE_9_Y = 160,  /* 第 9 行 */
    DISPLAY_LINE_10_Y = 180, /* 第 10 行 */
    DISPLAY_LINE_11_Y = 200, /* 第 11 行 */
    DISPLAY_LINE_12_Y = 220, /* 第 12 行 */
    DISPLAY_LINE_13_Y = 240, /* 第 13 行 */
    DISPLAY_LINE_14_Y = 260, /* 第 14 行 */
    DISPLAY_LINE_15_Y = 280, /* 第 15 行 */
    DISPLAY_LINE_16_Y = 300, /* 第 16 行 */
    DISPLAY_LINE_17_Y = 320, /* 第 17 行 */
    DISPLAY_LINE_18_Y = 340, /* 第 18 行 */
    DISPLAY_LINE_ERROR_Y = 0, /* 错误信息专用行 */
} display_line_y_t;

/* 显示刷新标志位（定时器 ISR 置1，task 清0） */
extern volatile uint8_t display_refresh_flag;

void display_task(void);
void display_show_error(const char *format, ...);

#endif
