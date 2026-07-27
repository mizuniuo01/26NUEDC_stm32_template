#ifndef AUTO_BALL_CAR_USER_DRIVERS_DRIVER_CAMERA_PROTOCOL_H
#define AUTO_BALL_CAR_USER_DRIVERS_DRIVER_CAMERA_PROTOCOL_H /* 头文件保护 */

#include "status.h"
#include <stdbool.h>
#include <stdint.h>

#define DRIVER_CAMERA_LOGICAL_CAPACITY 132U /* 解码后单帧缓冲区容量，单位：字节 */

/* MaixCAM 视觉目标数据 */
typedef struct {
    int16_t error_x;       /* 目标相对画面中心的横向像素偏差 */
    int16_t error_y;       /* 目标相对画面中心的纵向像素偏差 */
    bool has_target;         /* 检测到目标的标志 */
    bool has_switch_ack;     /* 视觉开关命令已应答标志 */
    uint8_t switch_ack_id;   /* 最近应答的视觉开关请求 ID */
    bool is_valid;           /* 目标数据有效标志 */
    uint32_t sequence;       /* 目标数据更新序号 */
} driver_camera_target_t;

/* MaixCAM 协议解析器状态 */
typedef struct {
    uint8_t frame[DRIVER_CAMERA_LOGICAL_CAPACITY]; /* 当前解码帧缓冲区 */
    uint16_t length;                       /* 当前帧已接收字节数 */
    uint8_t state;                         /* 协议解析状态 */
    driver_camera_target_t target;         /* 最近一次有效目标数据 */
} driver_camera_protocol_t;

/* 协议解析与数据查询接口 */
void driver_camera_protocol_init(driver_camera_protocol_t *protocol);
void driver_camera_protocol_push(driver_camera_protocol_t *protocol, const uint8_t *data,
    uint16_t length);
status_code_t driver_camera_protocol_snapshot(const driver_camera_protocol_t *protocol,
    driver_camera_target_t *target);

/* 视觉开关命令编码接口 */
status_code_t driver_camera_protocol_encode_switch(bool is_enabled, uint8_t request_id,
    uint8_t *frame, uint16_t capacity, uint16_t *length);

#endif /* AUTO_BALL_CAR_USER_DRIVERS_DRIVER_CAMERA_PROTOCOL_H */
