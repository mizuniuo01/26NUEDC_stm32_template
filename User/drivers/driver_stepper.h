#ifndef AUTO_BALL_CAR_USER_DRIVERS_DRIVER_STEPPER_H
#define AUTO_BALL_CAR_USER_DRIVERS_DRIVER_STEPPER_H /* 头文件保护 */

#include "status.h"
#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

#define DRIVER_STEPPER_RX_CAPACITY 16U /* 单次电机应答 DMA 接收容量，单位：字节 */
#define DRIVER_STEPPER_CONTROL_RESPONSE_SIZE 4U /* 控制命令应答帧长度，单位：字节 */
#define DRIVER_STEPPER_POSITION_RESPONSE_SIZE 8U /* 实时位置应答帧长度，单位：字节 */

/*
 * 当前实机配置（ZDT X42S 第二代，X 固件，单台电机）：
 *   锁定按键 Lock        = Disable
 *   控制模式             = CR_VFOC
 *   脉冲端口复用         = PUL_ENA；通讯端口复用 = UART
 *   En 有效电平          = Hold；Dir 正方向      = CW
 *   细分                 = 32；细分插补         = Enable
 *   自动熄屏             = Disable；低通滤波器   = Def
 *   开环工作电流         = 1200 mA
 *   闭环最大电流         = 2200 mA；闭环最大速度 = 3000 RPM
 *   电流环带宽           = 1000 Hz
 *   UART                 = 115200 8-N-1；CAN    = 500 kbit/s
 *   通讯校验             = 固定 0x6B；控制命令应答 = Receive
 *   通讯位置精度         = Enable；堵转保护   = Enable
 *   堵转检测             = 8 RPM / 2000 mA / 2000 ms
 *   位置到达窗口         = 0.5°
 *   电机地址             = 1（仅一台）
 *   接线                 = USART2 TX/RX + GND；未接 EN 脚
 *
 * 回零参数：Nearest（单圈就近）、方向 CW、速度 30 RPM、超时 10000 ms；
 * 无限位碰撞检测为 300 RPM / 800 mA / 60 ms；上电自动回零 Disable。
 * 以上是驱动器内部保存的参数，本驱动不会在每次启动时重复写入，避免
 * 未经确认的参数修改；发送的实时运动命令仍按手册的 X 固件自由协议编码。
 */

/* ZDT X42S FD 指令的位置模式 */
typedef enum {
    DRIVER_STEPPER_MODE_RELATIVE_TARGET = 0x00U, /* 相对上一目标位置 */
    DRIVER_STEPPER_MODE_ABSOLUTE = 0x01U,        /* 相对坐标零点 */
    DRIVER_STEPPER_MODE_RELATIVE_CURRENT = 0x02U, /* 相对当前实时位置 */
} driver_stepper_move_mode_t;

/* ZDT X42S 串口步进电机配置 */
typedef struct {
    UART_HandleTypeDef *uart; /* CubeMX 生成的串口句柄 */
    uint8_t id;               /* 步进电机总线 ID */
} driver_stepper_config_t;

/* 最近一次有效控制命令应答快照 */
typedef struct {
    uint8_t command;   /* 应答对应的功能码 */
    uint8_t code;      /* 02/12/E2/EE/9F 等手册返回码 */
    uint32_t sequence; /* 有效应答递增序号 */
    bool is_valid;     /* 已收到至少一条有效应答 */
} driver_stepper_response_t;

/* 最近一次有效实时位置快照 */
typedef struct {
    float angle_deg;   /* 实时位置角度，单位：度 */
    uint32_t sequence; /* 有效位置应答递增序号 */
    bool is_valid;     /* 已收到至少一条有效位置应答 */
} driver_stepper_position_t;

/* ZDT X42S 串口步进电机驱动实例 */
/* 主循环设置 is_busy，串口回调清除；目标 MCU 已验证 bool 访问原子性。 */
typedef struct {
    driver_stepper_config_t config; /* 配置快照 */
    uint8_t tx_buffer[16];          /* 最大实时命令（FD）发送缓冲区 */
    uint8_t rx_dma_buffer[DRIVER_STEPPER_RX_CAPACITY]; /* DMA 接收缓冲区 */
    uint8_t rx_frame[DRIVER_STEPPER_POSITION_RESPONSE_SIZE]; /* 应答解析暂存 */
    volatile uint32_t response_sequence; /* ISR 写、主循环读的应答序号 */
    volatile uint32_t position_sequence; /* ISR 写、主循环读的位置序号 */
    volatile uint32_t position_counts; /* ISR 写、主循环读的绝对角度计数 */
    volatile uint8_t response_command; /* ISR 写、主循环读的功能码 */
    volatile uint8_t response_code; /* ISR 写、主循环读的返回码 */
    volatile uint8_t position_sign; /* ISR 写、主循环读的位置符号 */
    uint8_t rx_length;             /* 当前暂存的应答字节数 */
    uint8_t rx_expected_length;    /* 当前应答预期总长度 */
    volatile bool is_busy;          /* UART DMA 发送进行中标志 */
    volatile bool response_valid;   /* ISR 写、主循环读的应答有效标志 */
    volatile bool position_valid;   /* ISR 写、主循环读的位置有效标志 */
    bool is_enabled;                /* 步进电机使能标志 */
    bool is_initialized;            /* 驱动初始化完成标志 */
} driver_stepper_t;

/* 生命周期与步进电机控制接口 */
status_code_t driver_stepper_init(driver_stepper_t *stepper, const driver_stepper_config_t *config);
status_code_t driver_stepper_enable(driver_stepper_t *stepper, bool is_enabled);
/* speed 使用手册单位 0.1 RPM（例如 3000 表示 300.0 RPM，30000 表示 3000.0 RPM）；
 * acceleration 同时作为加速、减速 RPM/s 参数。 */
status_code_t driver_stepper_move(driver_stepper_t *stepper, float angle, uint16_t speed,
    uint16_t acceleration, driver_stepper_move_mode_t mode, bool is_synchronized);
status_code_t driver_stepper_stop(driver_stepper_t *stepper);
status_code_t driver_stepper_clear_position(driver_stepper_t *stepper);
status_code_t driver_stepper_read_position(driver_stepper_t *stepper);
status_code_t driver_stepper_response_snapshot(driver_stepper_t *stepper,
    driver_stepper_response_t *response);
status_code_t driver_stepper_position_snapshot(driver_stepper_t *stepper,
    driver_stepper_position_t *position);

/* HAL 中断完成回调转发接口 */
void driver_stepper_rx_event_isr(driver_stepper_t *stepper, UART_HandleTypeDef *uart,
    uint16_t size);
void driver_stepper_tx_complete_isr(driver_stepper_t *stepper, UART_HandleTypeDef *uart);
void driver_stepper_error_isr(driver_stepper_t *stepper, UART_HandleTypeDef *uart);

#endif /* AUTO_BALL_CAR_USER_DRIVERS_DRIVER_STEPPER_H */
