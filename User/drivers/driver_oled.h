#ifndef AUTO_BALL_CAR_USER_DRIVERS_DRIVER_OLED_H
#define AUTO_BALL_CAR_USER_DRIVERS_DRIVER_OLED_H /* 头文件保护 */

#include "status.h"
#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

#define DRIVER_OLED_WIDTH_PIXELS 128U /* OLED 显示宽度，单位：像素 */
#define DRIVER_OLED_HEIGHT_PIXELS 64U /* OLED 显示高度，单位：像素 */
/* 单色页面布局显存容量，单位：字节 */
#define DRIVER_OLED_BUFFER_SIZE_BYTES                                                           \
    (DRIVER_OLED_WIDTH_PIXELS * DRIVER_OLED_HEIGHT_PIXELS / 8U)

/* SSD1306 I2C 驱动配置 */
typedef struct {
    I2C_HandleTypeDef *i2c; /* CubeMX 生成的 I2C 句柄 */
    uint16_t address;       /* HAL 接口使用的左移一位 I2C 地址 */
} driver_oled_config_t;

/* SSD1306 非阻塞刷新驱动实例 */
/* 主循环发起刷新，I2C 回调更新 volatile 标志；目标 MCU 已验证 bool 访问原子性。 */
typedef struct {
    driver_oled_config_t config;                 /* 配置快照 */
    uint8_t buffer[DRIVER_OLED_BUFFER_SIZE_BYTES];     /* 单色页面布局显存 */
    uint8_t tx_buffer[DRIVER_OLED_WIDTH_PIXELS + 1U];  /* 单页 DMA 发送缓冲区 */
    volatile bool is_busy;                       /* DMA 传输进行中标志 */
    volatile bool is_refresh_requested;          /* 等待执行的刷新请求标志 */
    uint8_t page;                                /* 当前发送页面索引 */
    uint8_t remaining_pages;                     /* 本轮尚未发送的页面数量 */
    bool is_initialized;                         /* 驱动初始化完成标志 */
} driver_oled_t;

/* 生命周期与显存操作接口 */
status_code_t driver_oled_init(driver_oled_t *oled, const driver_oled_config_t *config);
void driver_oled_clear(driver_oled_t *oled);
status_code_t driver_oled_set_pixel(driver_oled_t *oled, uint8_t x, uint8_t y, bool is_on);
status_code_t driver_oled_refresh(driver_oled_t *oled);
status_code_t driver_oled_process(driver_oled_t *oled);

/* HAL 中断完成回调转发接口 */
void driver_oled_tx_complete_isr(driver_oled_t *oled, I2C_HandleTypeDef *i2c);
void driver_oled_error_isr(driver_oled_t *oled, I2C_HandleTypeDef *i2c);

#endif /* AUTO_BALL_CAR_USER_DRIVERS_DRIVER_OLED_H */
