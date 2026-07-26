#ifndef DRIVER_LIBRARY_COMMON_LIBRARIES_OLED_DATA_H
#define DRIVER_LIBRARY_COMMON_LIBRARIES_OLED_DATA_H

#include <stdint.h>

/* ASCII 字模：6×8 点阵，95 个可打印字符（0x20~0x7E） */
extern const uint8_t oled_font_6x8[][6];
/* ASCII 字模：8×16 点阵，95 个可打印字符（0x20~0x7E） */
extern const uint8_t oled_font_8x16[][16];

#endif /* DRIVER_LIBRARY_COMMON_LIBRARIES_OLED_DATA_H */
