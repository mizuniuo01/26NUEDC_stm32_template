/*
 * F32C 无刷电机共享串口驱动公共接口。
 * 一个总线实例可注册多个电机实例；本项目约定 X 轴地址为 1，Y 轴地址为 2。
 * 所有命令接口均为非阻塞接口，只表示命令成功进入发送队列。
 */

/* 头文件重复包含保护宏。 */
#ifndef BLDC_H
#define BLDC_H

#include <stdint.h>
#include <stm32f4xx_hal.h>

/* 驱动接口返回状态。 */
typedef enum {
    BLDC_STATUS_OK = 0,                /* 操作成功。 */
    BLDC_STATUS_INVALID_ARGUMENT = -1, /* 参数、地址或实例无效。 */
    BLDC_STATUS_BUSY = -2,             /* HAL 串口暂时忙。 */
    BLDC_STATUS_IO_ERROR = -3,         /* 串口或协议帧错误。 */
    BLDC_STATUS_NOT_READY = -4,        /* 尚未收到请求的数据。 */
    BLDC_STATUS_QUEUE_FULL = -5,       /* 发送队列或接收 FIFO 已满。 */
} bldc_status_t;

/* F32C 控制模式，数值与设备协议保持一致。 */
typedef enum {
    BLDC_MODE_SPEED = 0,                       /* 速度控制模式。 */
    BLDC_MODE_MULTI_TURN_TRAPEZOIDAL = 1,      /* 多圈绝对位置 T 型规划模式。 */
    BLDC_MODE_SINGLE_TURN_TRAPEZOIDAL = 2,     /* 单圈绝对位置 T 型规划模式。 */
    BLDC_MODE_MULTI_TURN_DIRECT = 3,           /* 多圈绝对位置直通模式。 */
    BLDC_MODE_SINGLE_TURN_DIRECT = 4,          /* 单圈绝对位置直通模式。 */
} bldc_mode_t;

/* 可请求的设备反馈类型，数值与设备协议保持一致。 */
typedef enum {
    BLDC_FEEDBACK_SPEED = 0,          /* 当前速度反馈。 */
    BLDC_FEEDBACK_TOTAL_ANGLE,        /* 上电后累计角度反馈。 */
    BLDC_FEEDBACK_MECHANICAL_ANGLE,   /* 单圈机械角度反馈。 */
    BLDC_FEEDBACK_ACCELERATION,       /* 当前加速度反馈。 */
    BLDC_FEEDBACK_BUS_VOLTAGE,        /* 母线电压反馈。 */
    BLDC_FEEDBACK_COUNT,              /* 反馈类型数量，不是协议类型值。 */
} bldc_feedback_type_t;

/* bldc_telemetry_t::valid_mask 的字段有效位。 */
typedef enum {
    BLDC_FEEDBACK_VALID_SPEED = (1UL << BLDC_FEEDBACK_SPEED), /* 速度有效位。 */
    BLDC_FEEDBACK_VALID_TOTAL_ANGLE = (1UL << BLDC_FEEDBACK_TOTAL_ANGLE), /* 累计角度有效位。 */
    BLDC_FEEDBACK_VALID_MECHANICAL_ANGLE =
        (1UL << BLDC_FEEDBACK_MECHANICAL_ANGLE), /* 机械角度有效位。 */
    BLDC_FEEDBACK_VALID_ACCELERATION = (1UL << BLDC_FEEDBACK_ACCELERATION), /* 加速度有效位。 */
    BLDC_FEEDBACK_VALID_BUS_VOLTAGE = (1UL << BLDC_FEEDBACK_BUS_VOLTAGE), /* 母线电压有效位。 */
} bldc_feedback_valid_t;

/* 驱动固定配置与协议容量。 */
typedef enum {
    BLDC_X_ADDRESS = 1,             /* 云台 X 轴电机地址。 */
    BLDC_Y_ADDRESS = 2,             /* 云台 Y 轴电机地址。 */
    BLDC_DMA_RX_BUFFER_SIZE = 32,   /* 单次 DMA 接收缓冲区字节数。 */
    BLDC_RX_FIFO_CAPACITY = 128,    /* 接收环形队列容量。 */
    BLDC_TX_QUEUE_CAPACITY = 16,    /* 发送帧环形队列容量。 */
    BLDC_MAX_FRAME_SIZE = 9,        /* 协议最大帧长，单位为字节。 */
    BLDC_MAX_MOTOR_COUNT = 4,       /* 单条总线最多注册的电机数。 */
} bldc_config_value_t;

/* 电机最近一次有效反馈的只读快照。 */
typedef struct {
    int32_t speed_rpm;              /* 当前速度，单位为 RPM。 */
    float total_angle_deg;          /* 上电后累计角度，单位为度。 */
    float mechanical_angle_deg;     /* 单圈机械角度，范围为 0～359.9 度。 */
    float acceleration_rps2;        /* 加速度，单位为转/秒^2。 */
    float bus_voltage_v;            /* 母线电压，单位为伏特。 */
    uint32_t valid_mask;            /* 已收到的字段集合。 */
    uint32_t update_tick_ms[BLDC_FEEDBACK_COUNT]; /* 各字段最近更新时间。 */
} bldc_telemetry_t;

/* 驱动内部发送帧存储，生命周期由 bldc_bus_t 管理。 */
typedef struct {
    uint8_t data[BLDC_MAX_FRAME_SIZE];
    uint8_t length;
} bldc_tx_frame_t;

typedef struct bldc_motor bldc_motor_t;

/*
 * F32C 共享串口总线实例。
 * DMA 回调写入 rx_write_pos，bldc_task() 消费 rx_read_pos；发送完成回调更新
 * tx_read_pos 和 is_tx_busy。调用方只负责实例存储，不得直接修改成员。
 */
typedef struct {
    UART_HandleTypeDef *huart;
    uint8_t dma_rx_buffer[BLDC_DMA_RX_BUFFER_SIZE];
    uint8_t rx_fifo[BLDC_RX_FIFO_CAPACITY];
    volatile uint16_t rx_write_pos;
    volatile uint16_t rx_read_pos;
    uint8_t rx_frame[BLDC_MAX_FRAME_SIZE];
    uint8_t rx_frame_index;
    bldc_tx_frame_t tx_queue[BLDC_TX_QUEUE_CAPACITY];
    volatile uint8_t tx_write_pos;
    volatile uint8_t tx_read_pos;
    volatile uint8_t is_tx_busy;
    uint32_t last_tx_tick_ms;
    bldc_motor_t *motors[BLDC_MAX_MOTOR_COUNT];
    uint8_t motor_count;
    volatile bldc_status_t last_status;
} bldc_bus_t;

/* F32C 电机实例；由调用方持有存储，由所属总线更新反馈快照。 */
struct bldc_motor {
    bldc_bus_t *bus;
    uint8_t address;
    bldc_telemetry_t telemetry;
};

/* 实例初始化。 */
bldc_status_t bldc_bus_init(bldc_bus_t *bus, UART_HandleTypeDef *huart);
bldc_status_t bldc_motor_init(bldc_motor_t *motor, bldc_bus_t *bus, uint8_t address);

/* 基础控制命令。 */
bldc_status_t bldc_enable(bldc_motor_t *motor);
bldc_status_t bldc_disable(bldc_motor_t *motor);
bldc_status_t bldc_set_mode(bldc_motor_t *motor, bldc_mode_t mode);
bldc_status_t bldc_set_speed(bldc_motor_t *motor, int16_t speed_rpm);
bldc_status_t bldc_set_acceleration(bldc_motor_t *motor, uint16_t acceleration_rps2);
bldc_status_t bldc_set_multi_turn_angle(bldc_motor_t *motor, float angle_deg);
bldc_status_t bldc_set_single_turn_angle(bldc_motor_t *motor, float angle_deg);

/* 模式组合控制命令；整组命令保证全部入队或全部不入队。 */
bldc_status_t bldc_run_speed(bldc_motor_t *motor, int16_t speed_rpm, uint16_t acceleration_rps2);
bldc_status_t bldc_move_multi_turn_trapezoidal(bldc_motor_t *motor, float angle_deg,
    uint16_t speed_rpm, uint16_t acceleration_rps2);
bldc_status_t bldc_move_single_turn_trapezoidal(bldc_motor_t *motor, float angle_deg,
    uint16_t speed_rpm, uint16_t acceleration_rps2);
bldc_status_t bldc_move_multi_turn_direct(bldc_motor_t *motor, float angle_deg, uint16_t speed_rpm);
bldc_status_t bldc_move_single_turn_direct(bldc_motor_t *motor, float angle_deg,
    uint16_t speed_rpm);

/* 设备参数维护命令。 */
bldc_status_t bldc_save_parameters(bldc_motor_t *motor);
bldc_status_t bldc_clear_total_angle(bldc_motor_t *motor);
bldc_status_t bldc_set_mechanical_zero(bldc_motor_t *motor);

/* 异步反馈请求与缓存读取。 */
bldc_status_t bldc_request_feedback(bldc_motor_t *motor, bldc_feedback_type_t type);
bldc_status_t bldc_request_all_feedback(bldc_motor_t *motor);
bldc_status_t bldc_get_telemetry(const bldc_motor_t *motor, bldc_telemetry_t *telemetry);
bldc_status_t bldc_get_speed(const bldc_motor_t *motor, int32_t *speed_rpm);
bldc_status_t bldc_get_total_angle(const bldc_motor_t *motor, float *angle_deg);
bldc_status_t bldc_get_mechanical_angle(const bldc_motor_t *motor, float *angle_deg);
bldc_status_t bldc_get_acceleration(const bldc_motor_t *motor, float *acceleration_rps2);
bldc_status_t bldc_get_bus_voltage(const bldc_motor_t *motor, float *voltage_v);
bldc_status_t bldc_get_last_status(const bldc_bus_t *bus);

/* 主循环任务与 HAL UART 回调入口。 */
void bldc_task(bldc_bus_t *bus);
void bldc_rx_callback(bldc_bus_t *bus, UART_HandleTypeDef *huart, uint16_t size);
void bldc_tx_callback(bldc_bus_t *bus, UART_HandleTypeDef *huart);
void bldc_error_callback(bldc_bus_t *bus, UART_HandleTypeDef *huart);

#endif /* BLDC_H */
