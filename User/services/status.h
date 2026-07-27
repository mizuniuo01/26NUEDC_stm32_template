#ifndef AUTO_BALL_CAR_USER_SERVICES_STATUS_H
#define AUTO_BALL_CAR_USER_SERVICES_STATUS_H /* 头文件保护 */

#include <stdint.h>

/* 跨层接口使用的通用状态码 */
typedef enum {
    STATUS_OK = 0,               /* 操作成功 */
    STATUS_INVALID_ARGUMENT = 1, /* 参数为空或取值非法 */
    STATUS_NOT_INITIALIZED = 2,  /* 模块尚未初始化 */
    STATUS_BUSY = 3,             /* 硬件或模块正在执行其他事务 */
    STATUS_TIMEOUT = 4,          /* 操作未在约定时间内完成 */
    STATUS_IO_ERROR = 5,         /* 外设或传输接口报告错误 */
    STATUS_UNAVAILABLE = 6,      /* 请求的能力在当前硬件上不可用 */
    STATUS_OUT_OF_RANGE = 7,     /* 数值或索引超出支持范围 */
    STATUS_STATE_ERROR = 8,      /* 当前状态不允许执行该操作 */
} status_code_t;

/* 诊断事件的来源模块 */
typedef enum {
    STATUS_SOURCE_BOARD = 0,    /* BSP 板级组合根 */
    STATUS_SOURCE_MOTOR,        /* 电机驱动 */
    STATUS_SOURCE_ENCODER,      /* 编码器驱动 */
    STATUS_SOURCE_SENSOR,       /* 巡线传感器驱动 */
    STATUS_SOURCE_OLED,         /* OLED 显示驱动 */
    STATUS_SOURCE_KEY,          /* 按键驱动 */
    STATUS_SOURCE_ULTRASONIC,   /* 超声波测距驱动 */
    STATUS_SOURCE_BLUETOOTH,    /* 蓝牙通信链路 */
    STATUS_SOURCE_CAMERA,       /* 视觉通信链路 */
    STATUS_SOURCE_GYRO,         /* 姿态传感器通信链路 */
    STATUS_SOURCE_SERVO,        /* 总线舵机驱动 */
    STATUS_SOURCE_STEPPER,      /* 步进电机驱动 */
    STATUS_SOURCE_DOMAIN,       /* 领域逻辑 */
    STATUS_SOURCE_COUNT,        /* 来源枚举成员数量 */
} status_source_t;

#endif /* AUTO_BALL_CAR_USER_SERVICES_STATUS_H */
