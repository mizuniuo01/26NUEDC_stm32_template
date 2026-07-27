#ifndef AUTO_BALL_CAR_USER_DRIVERS_DRIVER_SENSOR_MCU_H
#define AUTO_BALL_CAR_USER_DRIVERS_DRIVER_SENSOR_MCU_H /* 头文件保护 */

#include "status.h"
#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

/* MCU 版巡线传感器 I2C 配置 */
typedef struct {
    I2C_HandleTypeDef *i2c; /* CubeMX 生成的 I2C 句柄 */
    uint16_t address;       /* HAL 接口使用的左移一位 I2C 地址 */
    uint8_t command;        /* 读取七路巡线状态的命令字 */
} driver_sensor_mcu_config_t;

/* MCU 版巡线传感器非阻塞驱动实例 */
/* I2C 回调写 volatile 快照，主循环读取；BSP 通过单次字段复制获取数据。 */
typedef struct {
    driver_sensor_mcu_config_t config; /* 配置快照 */
    volatile uint8_t raw;              /* 最近一次读取的巡线位掩码 */
    volatile bool is_busy;             /* I2C DMA 事务进行中标志 */
    volatile bool is_valid;            /* 最近一次数据有效标志 */
    volatile uint32_t sequence;        /* 数据更新序号 */
    volatile uint32_t timestamp_ms;    /* 最近更新时间，单位：毫秒 */
    bool is_initialized;               /* 驱动初始化完成标志 */
} driver_sensor_mcu_t;

/* 生命周期与非阻塞采集接口 */
status_code_t driver_sensor_mcu_init(driver_sensor_mcu_t *sensor,
    const driver_sensor_mcu_config_t *config);
status_code_t driver_sensor_mcu_request(driver_sensor_mcu_t *sensor);

/* HAL 中断完成回调转发接口 */
void driver_sensor_mcu_rx_complete_isr(driver_sensor_mcu_t *sensor, I2C_HandleTypeDef *i2c,
    uint32_t timestamp_ms);
void driver_sensor_mcu_error_isr(driver_sensor_mcu_t *sensor, I2C_HandleTypeDef *i2c);

/* 最近一次采集结果查询接口 */
status_code_t driver_sensor_mcu_snapshot(const driver_sensor_mcu_t *sensor, uint8_t *value,
    bool *is_valid, uint32_t *sequence, uint32_t *timestamp_ms);

#endif /* AUTO_BALL_CAR_USER_DRIVERS_DRIVER_SENSOR_MCU_H */
