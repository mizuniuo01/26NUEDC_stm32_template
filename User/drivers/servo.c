/**
 * @file    servo.c
 * @brief   Fashion Star 单舵机非阻塞串口驱动。
 * @note    仅实现单圈绝对角度控制，不依赖官方驱动运行库。
 * @note    UART 采用中断发送；发送完成 ISR 只置位，队列由 servo_task() 推进。
 * @note    本工程中舵机安装完成后，初始姿态对应的机械角度固定不变，运行时只在该初始
 *          位置与相差 90 度的工作位置之间往返。两个端点均提前标定为相对舵机机械零点
 *          的绝对角度，由上层直接调用 servo_move() 切换，因此驱动不维护相对位移，也
 *          不持续查询实际角度。servo_init() 只初始化软件状态；系统若需要保证启动姿态，
 *          应在初始化完成后显式发送一次初始位置命令，该命令不会修改舵机机械零点。
 * @warning 本驱动不查询位置，因此成功返回只代表命令已进入发送队列。
 */

#include "servo.h"

#include <math.h>
#include <string.h>

/* Fashion Star 单圈角度控制协议常量。 */
typedef enum {
    SERVO_FRAME_HEADER_LOW = 0x12U,  /* 请求帧头低字节。 */
    SERVO_FRAME_HEADER_HIGH = 0x4CU, /* 请求帧头高字节。 */
    SERVO_ROTATE_COMMAND = 0x08U,    /* 单圈角度控制命令。 */
    SERVO_ROTATE_PAYLOAD_SIZE = 7U,  /* 单圈角度控制负载长度。 */
    SERVO_ROTATE_FRAME_SIZE = 12U,   /* 完整控制帧长度。 */
    SERVO_TX_QUEUE_CAPACITY = 4U,    /* 固定发送队列帧数。 */
    SERVO_MAX_ID = 254U,             /* 最大可配置舵机地址。 */
    SERVO_MAX_ANGLE_TENTHS = 1800U,  /* 协议最大单圈角度，单位为 0.1 度。 */
} servo_protocol_value_t;

/* 单实例驱动的私有状态。 */
typedef struct {
    servo_config_t config; /* BSP 注入的固定配置副本。 */
    uint8_t tx_queue[SERVO_TX_QUEUE_CAPACITY][SERVO_ROTATE_FRAME_SIZE];
    uint8_t tx_read_pos;              /* 当前发送帧索引。 */
    uint8_t tx_write_pos;             /* 下一帧写入索引。 */
    uint8_t tx_count;                 /* 队列内帧数，包含正在发送的帧。 */
    uint8_t is_tx_busy;               /* UART 中断发送进行中标志。 */
    volatile uint8_t tx_complete_flag; /* ISR 写、主循环读的发送完成标志。 */
    uint8_t is_initialized;           /* 驱动初始化完成标志。 */
    servo_status_t last_status;       /* 最近一次 UART 调度状态。 */
} servo_instance_t;

static servo_instance_t servo_instance;

/**
 * @brief  检查驱动初始化配置是否合法。
 * @param  config 待检查的单实例配置。
 * @return 配置合法返回 SERVO_STATUS_OK，否则返回 SERVO_STATUS_INVALID_ARGUMENT。
 */
static servo_status_t validate_config(const servo_config_t *config)
{
    if ((!config) || (!config->huart)) {
        return SERVO_STATUS_INVALID_ARGUMENT;
    }

    if (config->servo_id > SERVO_MAX_ID) {
        return SERVO_STATUS_INVALID_ARGUMENT;
    }

    if ((config->positive_direction != SERVO_POSITIVE_ANGLE_CLOCKWISE) &&
        (config->positive_direction != SERVO_POSITIVE_ANGLE_COUNTERCLOCKWISE)) {
        return SERVO_STATUS_INVALID_ARGUMENT;
    }

    if ((!isfinite(config->clockwise_limit_deg)) ||
        (!isfinite(config->counterclockwise_limit_deg)) ||
        (config->clockwise_limit_deg < 0.0f) ||
        (config->counterclockwise_limit_deg < 0.0f) ||
        (config->clockwise_limit_deg > 180.0f) ||
        (config->counterclockwise_limit_deg > 180.0f) ||
        (config->default_interval_ms == 0U)) {
        return SERVO_STATUS_INVALID_ARGUMENT;
    }

    return SERVO_STATUS_OK;
}

/**
 * @brief  将 UART HAL 状态转换为舵机驱动状态。
 * @param  hal_status HAL UART 接口返回状态。
 * @return 对应的舵机驱动状态。
 */
static servo_status_t map_hal_status(HAL_StatusTypeDef hal_status)
{
    if (hal_status == HAL_OK) {
        return SERVO_STATUS_OK;
    }
    if (hal_status == HAL_BUSY) {
        return SERVO_STATUS_BUSY;
    }
    return SERVO_STATUS_IO_ERROR;
}

/**
 * @brief  计算 Fashion Star 请求帧的累加校验值。
 * @param  frame 已填入帧头和负载的控制帧。
 * @return 帧中校验字段之前所有字节之和的低八位。
 */
static uint8_t calculate_checksum(const uint8_t *frame)
{
    uint16_t checksum = 0U;
    uint8_t index;

    for (index = 0U; index < (SERVO_ROTATE_FRAME_SIZE - 1U); index++) {
        checksum += frame[index];
    }

    return (uint8_t)checksum;
}

/**
 * @brief  判断给定方向是否应编码为协议正角。
 * @param  direction 调用方指定的物理运动方向。
 * @return 应使用正角返回非零值，应使用负角返回零。
 */
static uint8_t is_positive_angle(servo_direction_t direction)
{
    if (servo_instance.config.positive_direction == SERVO_POSITIVE_ANGLE_CLOCKWISE) {
        return (direction == SERVO_DIRECTION_CLOCKWISE) ? 1U : 0U;
    }

    return (direction == SERVO_DIRECTION_COUNTERCLOCKWISE) ? 1U : 0U;
}

/**
 * @brief  组装一帧单圈绝对角度控制命令。
 * @param  frame 输出帧存储区，容量必须为 SERVO_ROTATE_FRAME_SIZE。
 * @param  direction 目标物理方向。
 * @param  angle_deg 相对机械零点的非负角度幅值，单位为度。
 * @return 组帧成功返回 SERVO_STATUS_OK，参数非法或越界返回参数错误。
 */
static servo_status_t build_rotate_frame(uint8_t *frame, servo_direction_t direction,
    float angle_deg)
{
    float angle_limit_deg;
    uint16_t angle_magnitude_tenths;
    int16_t signed_angle_tenths;

    if ((!frame) ||
        ((direction != SERVO_DIRECTION_CLOCKWISE) &&
         (direction != SERVO_DIRECTION_COUNTERCLOCKWISE)) ||
        (!isfinite(angle_deg)) || (angle_deg < 0.0f)) {
        return SERVO_STATUS_INVALID_ARGUMENT;
    }

    angle_limit_deg = (direction == SERVO_DIRECTION_CLOCKWISE)
                          ? servo_instance.config.clockwise_limit_deg
                          : servo_instance.config.counterclockwise_limit_deg;
    if (angle_deg > angle_limit_deg) {
        return SERVO_STATUS_INVALID_ARGUMENT;
    }

    angle_magnitude_tenths = (uint16_t)((angle_deg * 10.0f) + 0.5f);
    if (angle_magnitude_tenths > SERVO_MAX_ANGLE_TENTHS) {
        return SERVO_STATUS_INVALID_ARGUMENT;
    }

    signed_angle_tenths = (int16_t)angle_magnitude_tenths;
    if (!is_positive_angle(direction)) {
        signed_angle_tenths = (int16_t)-signed_angle_tenths;
    }

    frame[0] = SERVO_FRAME_HEADER_LOW;
    frame[1] = SERVO_FRAME_HEADER_HIGH;
    frame[2] = SERVO_ROTATE_COMMAND;
    frame[3] = SERVO_ROTATE_PAYLOAD_SIZE;
    frame[4] = servo_instance.config.servo_id;
    frame[5] = (uint8_t)((uint16_t)signed_angle_tenths & 0xFFU);
    frame[6] = (uint8_t)(((uint16_t)signed_angle_tenths >> 8U) & 0xFFU);
    frame[7] = (uint8_t)(servo_instance.config.default_interval_ms & 0xFFU);
    frame[8] = (uint8_t)((servo_instance.config.default_interval_ms >> 8U) & 0xFFU);
    frame[9] = (uint8_t)(servo_instance.config.power_mw & 0xFFU);
    frame[10] = (uint8_t)((servo_instance.config.power_mw >> 8U) & 0xFFU);
    frame[11] = calculate_checksum(frame);

    return SERVO_STATUS_OK;
}

/**
 * @brief  尝试启动队首帧的 UART 中断发送。
 * @return 已在发送或成功启动返回 SERVO_STATUS_OK；HAL 忙或失败返回对应状态。
 */
static servo_status_t start_transmit(void)
{
    HAL_StatusTypeDef hal_status;

    if ((servo_instance.is_tx_busy != 0U) || (servo_instance.tx_count == 0U)) {
        return SERVO_STATUS_OK;
    }

    hal_status = HAL_UART_Transmit_IT(servo_instance.config.huart,
        servo_instance.tx_queue[servo_instance.tx_read_pos], SERVO_ROTATE_FRAME_SIZE);
    servo_instance.last_status = map_hal_status(hal_status);
    if (hal_status == HAL_OK) {
        servo_instance.is_tx_busy = 1U;
    }

    return servo_instance.last_status;
}

/**
 * @brief  初始化单实例舵机驱动。
 * @param  config UART、地址、方向映射和运动参数配置。
 * @return 初始化成功返回 SERVO_STATUS_OK；配置非法或发送尚未结束时返回错误。
 */
servo_status_t servo_init(const servo_config_t *config)
{
    servo_status_t status;

    status = validate_config(config);
    if (status != SERVO_STATUS_OK) {
        return status;
    }

    if ((servo_instance.is_initialized != 0U) && (servo_instance.is_tx_busy != 0U)) {
        return SERVO_STATUS_BUSY;
    }

    memset(&servo_instance, 0, sizeof(servo_instance));
    servo_instance.config = *config;
    servo_instance.last_status = SERVO_STATUS_OK;
    servo_instance.is_initialized = 1U;

    return SERVO_STATUS_OK;
}

/**
 * @brief  非阻塞设置相对机械零点的单圈绝对角度。
 * @param  direction 从输出轴端观察的顺时针或逆时针方向。
 * @param  angle_deg 非负角度幅值，单位为度。
 * @return 命令入队返回 SERVO_STATUS_OK；未初始化、参数非法或队满返回对应状态。
 */
servo_status_t servo_move(servo_direction_t direction, float angle_deg)
{
    servo_status_t status;
    uint8_t *frame;

    if (servo_instance.is_initialized == 0U) {
        return SERVO_STATUS_NOT_INITIALIZED;
    }

    if (servo_instance.tx_count >= SERVO_TX_QUEUE_CAPACITY) {
        return SERVO_STATUS_QUEUE_FULL;
    }

    frame = servo_instance.tx_queue[servo_instance.tx_write_pos];
    status = build_rotate_frame(frame, direction, angle_deg);
    if (status != SERVO_STATUS_OK) {
        return status;
    }

    servo_instance.tx_write_pos++;
    if (servo_instance.tx_write_pos >= SERVO_TX_QUEUE_CAPACITY) {
        servo_instance.tx_write_pos = 0U;
    }
    servo_instance.tx_count++;

    (void)start_transmit();
    return SERVO_STATUS_OK;
}

/**
 * @brief  推进非阻塞发送队列。
 * @return 无。
 * @note   必须在主循环调用；ISR 仅通过 servo_tx_callback() 置位完成标志。
 */
void servo_task(void)
{
    if (servo_instance.is_initialized == 0U) {
        return;
    }

    if (servo_instance.tx_complete_flag != 0U) {
        servo_instance.tx_complete_flag = 0U;
        servo_instance.is_tx_busy = 0U;

        if (servo_instance.tx_count > 0U) {
            servo_instance.tx_read_pos++;
            if (servo_instance.tx_read_pos >= SERVO_TX_QUEUE_CAPACITY) {
                servo_instance.tx_read_pos = 0U;
            }
            servo_instance.tx_count--;
        }
    }

    (void)start_transmit();
}

/**
 * @brief  接收 UART 中断发送完成事件。
 * @param  huart 触发回调的 UART 句柄。
 * @return 无。
 * @note   本函数在 ISR 中调用，只执行实例匹配和标志位置位。
 */
void servo_tx_callback(UART_HandleTypeDef *huart)
{
    if ((servo_instance.is_initialized == 0U) || (!huart) ||
        (huart->Instance != servo_instance.config.huart->Instance)) {
        return;
    }

    servo_instance.tx_complete_flag = 1U;
}

/**
 * @brief  获取最近一次 UART 发送调度状态。
 * @return 最近一次 HAL UART 启动结果对应的驱动状态。
 */
servo_status_t servo_get_last_status(void)
{
    if (servo_instance.is_initialized == 0U) {
        return SERVO_STATUS_NOT_INITIALIZED;
    }

    return servo_instance.last_status;
}
