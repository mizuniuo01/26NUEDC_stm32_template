#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>

#include <stm32f4xx_hal.h>

/* 舵机驱动接口状态。 */
typedef enum {
    SERVO_STATUS_OK = 0,                /* 操作成功。 */
    SERVO_STATUS_INVALID_ARGUMENT = -1, /* 参数、配置或方向无效。 */
    SERVO_STATUS_NOT_INITIALIZED = -2,  /* 驱动尚未初始化。 */
    SERVO_STATUS_QUEUE_FULL = -3,       /* 非阻塞发送队列已满。 */
    SERVO_STATUS_BUSY = -4,             /* UART 暂时无法启动发送。 */
    SERVO_STATUS_IO_ERROR = -5,         /* UART 启动发送失败。 */
} servo_status_t;

/* 从舵机输出轴端观察时的目标方向。 */
typedef enum {
    SERVO_DIRECTION_CLOCKWISE = 0,        /* 顺时针方向。 */
    SERVO_DIRECTION_COUNTERCLOCKWISE = 1, /* 逆时针方向。 */
} servo_direction_t;

/* 官方协议正角与实际安装方向的映射。 */
typedef enum {
    SERVO_POSITIVE_ANGLE_CLOCKWISE = 0,        /* 正角驱动顺时针运动。 */
    SERVO_POSITIVE_ANGLE_COUNTERCLOCKWISE = 1, /* 正角驱动逆时针运动。 */
} servo_positive_direction_t;

/* 单实例舵机的固定配置，由 BSP 在初始化时注入。 */
typedef struct {
    UART_HandleTypeDef *huart;                     /* 舵机占用的 UART 句柄。 */
    uint8_t servo_id;                              /* 舵机地址，合法范围为 0～254。 */
    servo_positive_direction_t positive_direction; /* 正角对应的物理方向。 */
    float clockwise_limit_deg;                    /* 顺时针最大绝对角度，单位为度。 */
    float counterclockwise_limit_deg;             /* 逆时针最大绝对角度，单位为度。 */
    uint16_t default_interval_ms;                  /* 默认到达时间，单位为毫秒。 */
    uint16_t power_mw;                             /* 协议功率字段，单位为毫瓦。 */
} servo_config_t;

/*
 * 初始化单实例驱动，只保存配置并清空软件队列，不发送任何舵机命令。
 * 只能在任务或初始化上下文调用，不能在 ISR 中调用。
 */
servo_status_t servo_init(const servo_config_t *config);

/*
 * 非阻塞设置零点一侧的绝对角度；成功只表示命令进入发送队列。
 * angle_deg 为非负角度幅值，不能在 ISR 中调用。
 */
servo_status_t servo_move(servo_direction_t direction, float angle_deg);

/* 在主循环中反复调用，回收已发送帧并启动下一次 UART 中断发送。 */
void servo_task(void);

/* 从 HAL_UART_TxCpltCallback 转发发送完成事件，ISR 中调用。 */
void servo_tx_callback(UART_HandleTypeDef *huart);

/* 获取最近一次 UART 调度状态；该状态不表示舵机已经到位。 */
servo_status_t servo_get_last_status(void);

#endif /* SERVO_H */
