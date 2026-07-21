/**
 * @file    servo.c
 * @brief   Fashion Star 三舵机共享总线异步驱动。
 * @note    三个舵机共用 UART4，使用普通单舵机单圈角度命令独立控制。
 * @note    驱动不使用同步命令、批量暂存执行模式或角度查询命令。
 * @note    UART 采用中断发送；发送完成 ISR 只置位，队列由 servo_task() 推进。
 * @note    初始化不会设置机械零点，也不会主动改变任一舵机角度。
 * @warning servo_set_angle() 成功只表示命令进入发送队列，不表示舵机已经到位。
 */

#include "servo.h"

#include <math.h>
#include <string.h>

/** @brief Fashion Star 单圈角度控制协议和驱动容量常量。 */
typedef enum {
    SERVO_REQUEST_HEADER_LOW = 0x12U,  /**< 请求帧头低字节。 */
    SERVO_REQUEST_HEADER_HIGH = 0x4CU, /**< 请求帧头高字节。 */
    SERVO_COMMAND_SET_ANGLE = 0x08U,   /**< 普通单舵机单圈角度命令。 */
    SERVO_SET_ANGLE_PAYLOAD_SIZE = 7U, /**< 设置角度命令负载长度。 */
    SERVO_SET_ANGLE_FRAME_SIZE = 12U,  /**< 设置角度命令完整帧长度。 */
    SERVO_TX_QUEUE_CAPACITY = 8U,      /**< 非阻塞发送队列容量。 */
    SERVO_INSTANCE_CAPACITY = 3U,      /**< 本项目舵机实例数量。 */
    SERVO_DEFAULT_INTERVAL_MS = 100U,  /**< 默认到达目标角度的时间。 */
    SERVO_DEFAULT_POWER_MW = 0U,       /**< 使用舵机默认功率限制。 */
} servo_protocol_value_t;

/** @brief 三个舵机共用的 UART 发送状态。 */
typedef struct {
    UART_HandleTypeDef *huart;                                  /**< UART4 句柄。 */
    servo_t *servos[SERVO_INSTANCE_CAPACITY];                   /**< 已注册舵机。 */
    uint8_t tx_queue[SERVO_TX_QUEUE_CAPACITY][SERVO_SET_ANGLE_FRAME_SIZE];
    uint8_t servo_count;                                        /**< 已注册数量。 */
    uint8_t tx_read_pos;                                        /**< 队首位置。 */
    uint8_t tx_write_pos;                                       /**< 下一写入位置。 */
    uint8_t tx_count;                                           /**< 待发送帧数。 */
    volatile uint8_t is_tx_busy;                                /**< UART 发送标志。 */
    volatile uint8_t tx_complete_flag;                          /**< ISR 完成标志。 */
    uint8_t is_initialized;                                     /**< 总线初始化标志。 */
} servo_bus_t;

/** @brief 工程唯一的 Fashion Star 舵机共享总线。 */
static servo_bus_t servo_bus;

/**
 * @brief  计算协议帧的累加校验值。
 * @param  data    参与校验的数据首地址。
 * @param  length  参与校验的字节数。
 * @return 所有字节累加和的低八位。
 */
static uint8_t servo_calculate_checksum(const uint8_t *data, uint8_t length)
{
    uint16_t checksum = 0U;
    uint8_t index;

    for (index = 0U; index < length; index++) {
        checksum += data[index];
    }
    return (uint8_t)checksum;
}

/**
 * @brief  判断地址是否为本项目定义的三个舵机地址之一。
 * @param  address  待检查地址。
 * @return 地址有效返回 1，否则返回 0。
 */
static uint8_t servo_is_supported_address(servo_address_t address)
{
    return ((address == SERVO_ADDRESS_TRIGGER) || (address == SERVO_ADDRESS_X_AXIS)
            || (address == SERVO_ADDRESS_Y_AXIS))
               ? 1U
               : 0U;
}

/**
 * @brief  按地址查找已经注册的舵机实例。
 * @param  address  舵机总线地址。
 * @return 匹配的实例指针；不存在时返回 NULL。
 */
static servo_t *servo_find_by_address(uint8_t address)
{
    uint8_t index;

    for (index = 0U; index < servo_bus.servo_count; index++) {
        if (servo_bus.servos[index]->address == address) {
            return servo_bus.servos[index];
        }
    }
    return NULL;
}

/**
 * @brief  判断舵机实例是否已经注册到共享总线。
 * @param  servo  待检查实例。
 * @return 已注册返回 1，否则返回 0。
 */
static uint8_t servo_is_registered(const servo_t *servo)
{
    uint8_t index;

    if (servo == NULL) {
        return 0U;
    }

    for (index = 0U; index < servo_bus.servo_count; index++) {
        if (servo_bus.servos[index] == servo) {
            return 1U;
        }
    }
    return 0U;
}

/**
 * @brief  尝试启动队首角度命令的 UART 中断发送。
 * @return 无。
 * @note   UART 暂时忙时保留队首帧，由后续 servo_task() 再次尝试。
 */
static void servo_start_transmit(void)
{
    HAL_StatusTypeDef hal_status;

    if ((servo_bus.is_tx_busy != 0U) || (servo_bus.tx_count == 0U)) {
        return;
    }

    hal_status = HAL_UART_Transmit_IT(
        servo_bus.huart,
        servo_bus.tx_queue[servo_bus.tx_read_pos],
        SERVO_SET_ANGLE_FRAME_SIZE);
    if (hal_status == HAL_OK) {
        servo_bus.is_tx_busy = 1U;
    }
}

/**
 * @brief  初始化三个舵机共用的 UART 总线。
 * @param  huart  已完成 115200、8N1 和全局中断配置的 UART4 句柄。
 * @return 初始化状态。
 */
servo_status_t servo_bus_init(UART_HandleTypeDef *huart)
{
    if (huart == NULL) {
        return SERVO_STATUS_INVALID_ARGUMENT;
    }

    memset(&servo_bus, 0, sizeof(servo_bus));
    servo_bus.huart = huart;
    servo_bus.is_initialized = 1U;
    return SERVO_STATUS_OK;
}

/**
 * @brief  在共享总线上注册一个项目舵机实例。
 * @param  servo    调用方持有的实例存储。
 * @param  address  扳机、X 轴或 Y 轴地址。
 * @return 注册结果。
 */
servo_status_t servo_init(servo_t *servo, servo_address_t address)
{
    if (servo_bus.is_initialized == 0U) {
        return SERVO_STATUS_NOT_INITIALIZED;
    }
    if ((servo == NULL) || !servo_is_supported_address(address)
        || (servo_bus.servo_count >= SERVO_INSTANCE_CAPACITY)
        || (servo_find_by_address((uint8_t)address) != NULL)) {
        return SERVO_STATUS_INVALID_ARGUMENT;
    }

    memset(servo, 0, sizeof(*servo));
    servo->address = (uint8_t)address;
    servo->is_initialized = 1U;
    servo_bus.servos[servo_bus.servo_count] = servo;
    servo_bus.servo_count++;
    return SERVO_STATUS_OK;
}

/**
 * @brief  非阻塞设置指定舵机的单圈绝对角度。
 * @param  servo      system 提供的舵机实例指针。
 * @param  angle_deg  相对舵机机械零点的目标角度，范围为 -135～+135 度。
 * @return 命令入队成功返回 OK；参数非法、未初始化或队满时返回对应状态。
 */
servo_status_t servo_set_angle(servo_t *servo, float angle_deg)
{
    uint8_t *frame;
    int16_t angle_tenths;

    if ((servo_bus.is_initialized == 0U) || !servo_is_registered(servo)) {
        return SERVO_STATUS_NOT_INITIALIZED;
    }
    if (!isfinite(angle_deg) || (angle_deg < SERVO_MIN_ANGLE_DEG)
        || (angle_deg > SERVO_MAX_ANGLE_DEG)) {
        return SERVO_STATUS_INVALID_ARGUMENT;
    }
    if (servo_bus.tx_count >= SERVO_TX_QUEUE_CAPACITY) {
        return SERVO_STATUS_QUEUE_FULL;
    }

    angle_tenths = (angle_deg >= 0.0F)
                       ? (int16_t)((angle_deg * 10.0F) + 0.5F)
                       : (int16_t)((angle_deg * 10.0F) - 0.5F);
    frame = servo_bus.tx_queue[servo_bus.tx_write_pos];
    frame[0] = SERVO_REQUEST_HEADER_LOW;
    frame[1] = SERVO_REQUEST_HEADER_HIGH;
    frame[2] = SERVO_COMMAND_SET_ANGLE;
    frame[3] = SERVO_SET_ANGLE_PAYLOAD_SIZE;
    frame[4] = servo->address;
    frame[5] = (uint8_t)((uint16_t)angle_tenths & 0xFFU);
    frame[6] = (uint8_t)(((uint16_t)angle_tenths >> 8U) & 0xFFU);
    frame[7] = (uint8_t)(SERVO_DEFAULT_INTERVAL_MS & 0xFFU);
    frame[8] = (uint8_t)((SERVO_DEFAULT_INTERVAL_MS >> 8U) & 0xFFU);
    frame[9] = (uint8_t)(SERVO_DEFAULT_POWER_MW & 0xFFU);
    frame[10] = (uint8_t)((SERVO_DEFAULT_POWER_MW >> 8U) & 0xFFU);
    frame[11] = servo_calculate_checksum(frame, SERVO_SET_ANGLE_FRAME_SIZE - 1U);

    servo_bus.tx_write_pos =
        (uint8_t)((servo_bus.tx_write_pos + 1U) % SERVO_TX_QUEUE_CAPACITY);
    servo_bus.tx_count++;
    servo_start_transmit();
    return SERVO_STATUS_OK;
}

/**
 * @brief  推进非阻塞 UART 发送队列。
 * @return 无。
 * @note   必须在主循环调用；ISR 仅通过 servo_tx_callback() 置位完成标志。
 */
void servo_task(void)
{
    if (servo_bus.is_initialized == 0U) {
        return;
    }

    if (servo_bus.tx_complete_flag != 0U) {
        servo_bus.tx_complete_flag = 0U;
        servo_bus.is_tx_busy = 0U;
        if (servo_bus.tx_count > 0U) {
            servo_bus.tx_read_pos =
                (uint8_t)((servo_bus.tx_read_pos + 1U) % SERVO_TX_QUEUE_CAPACITY);
            servo_bus.tx_count--;
        }
    }

    servo_start_transmit();
}

/**
 * @brief  接收 UART 中断发送完成事件。
 * @param  huart  触发回调的 UART 句柄。
 * @return 无。
 * @note   本函数在 ISR 中调用，只执行实例匹配和标志位置位。
 */
void servo_tx_callback(UART_HandleTypeDef *huart)
{
    if ((servo_bus.is_initialized == 0U) || (huart == NULL)
        || (huart->Instance != servo_bus.huart->Instance)) {
        return;
    }

    servo_bus.tx_complete_flag = 1U;
}

/**
 * @brief  接收 UART 错误事件，使未完成的队首命令可以重新发送。
 * @param  huart  触发回调的 UART 句柄。
 * @return 无。
 * @note   本函数不在 ISR 中重启发送，后续由 servo_task() 统一推进。
 */
void servo_error_callback(UART_HandleTypeDef *huart)
{
    if ((servo_bus.is_initialized == 0U) || (huart == NULL)
        || (huart->Instance != servo_bus.huart->Instance)) {
        return;
    }

    servo_bus.is_tx_busy = 0U;
}
