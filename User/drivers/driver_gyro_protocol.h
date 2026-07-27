#ifndef AUTO_BALL_CAR_USER_DRIVERS_DRIVER_GYRO_PROTOCOL_H
#define AUTO_BALL_CAR_USER_DRIVERS_DRIVER_GYRO_PROTOCOL_H /* 头文件保护 */

#include "status.h"
#include <stdbool.h>
#include <stdint.h>

/* WIT 姿态传感器角度数据 */
typedef struct {
    float roll;        /* 横滚角，单位：度 */
    float pitch;       /* 俯仰角，单位：度 */
    float yaw;         /* 航向角，单位：度 */
    bool is_valid;     /* 姿态数据有效标志 */
    uint32_t sequence; /* 姿态数据更新序号 */
} driver_gyro_attitude_t;

/* WIT 0x55/0x53 帧解析器状态 */
typedef struct {
    uint8_t frame[11];                    /* 当前协议帧缓冲区 */
    uint8_t index;                        /* 下一字节的写入索引 */
    driver_gyro_attitude_t attitude;      /* 最近一次有效姿态数据 */
} driver_gyro_protocol_t;

/* 生命周期、协议解析与数据查询接口 */
void driver_gyro_protocol_init(driver_gyro_protocol_t *protocol);
void driver_gyro_protocol_push(driver_gyro_protocol_t *protocol, const uint8_t *data,
    uint16_t length);
status_code_t driver_gyro_protocol_snapshot(const driver_gyro_protocol_t *protocol,
    driver_gyro_attitude_t *attitude);

#endif /* AUTO_BALL_CAR_USER_DRIVERS_DRIVER_GYRO_PROTOCOL_H */
