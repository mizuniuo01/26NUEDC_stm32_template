/**
 * @file    bldc.c
 * @brief   F32C 无刷电机共享串口协议、异步发送队列与反馈解析实现。
 * @note    依赖 STM32 HAL UART DMA 空闲接收，USART 和 DMA 必须先由平台层初始化。
 * @note    手册要求多电机命令间隔至少 1 ms，本驱动从上一帧发送完成后开始计时。
 * @warning 控制和查询接口只能在主循环上下文调用，HAL 回调接口只能在 ISR 中调用。
 */

#include "bldc.h"

#include <string.h>

/** @brief F32C 协议字段、功能码和驱动边界常量。 */
typedef enum {
    BLDC_FRAME_HEADER = 0x7A,                  /**< 协议帧头。 */
    BLDC_FRAME_TAIL = 0x7B,                    /**< 协议帧尾。 */
    BLDC_FUNCTION_MODE = 0x00,                 /**< 控制模式功能码。 */
    BLDC_FUNCTION_SPEED = 0x01,                /**< 目标速度功能码。 */
    BLDC_FUNCTION_MULTI_TURN_ANGLE = 0x02,     /**< 多圈绝对角度功能码。 */
    BLDC_FUNCTION_SINGLE_TURN_ANGLE = 0x03,    /**< 单圈绝对角度功能码。 */
    BLDC_FUNCTION_DISABLE = 0x05,              /**< 电机失能功能码。 */
    BLDC_FUNCTION_ENABLE = 0x06,               /**< 电机使能功能码。 */
    BLDC_FUNCTION_ACCELERATION = 0x07,         /**< 加速度功能码。 */
    BLDC_FUNCTION_SAVE_PARAMETERS = 0x08,      /**< 参数掉电保存功能码。 */
    BLDC_FUNCTION_CLEAR_TOTAL_ANGLE = 0x09,    /**< 累计角度清零功能码。 */
    BLDC_FUNCTION_SET_MECHANICAL_ZERO = 0x0A,  /**< 机械角度置零功能码。 */
    BLDC_FUNCTION_REQUEST_FEEDBACK = 0x0E,     /**< 数据反馈请求功能码。 */
    BLDC_FEEDBACK_FRAME_SIZE = 9,              /**< 固定反馈帧字节数。 */
    BLDC_MIN_COMMAND_INTERVAL_MS = 1,          /**< 命令间最小空闲时间。 */
    BLDC_MAX_SPEED_RPM = 1000,                 /**< 允许的最大速度绝对值。 */
    BLDC_MAX_SINGLE_TURN_ANGLE_X10 = 3599,     /**< 单圈角度上限，单位 0.1 度。 */
    BLDC_MAX_MULTI_TURN_ANGLE_X10 = 18773852,  /**< 多圈角度上限，单位 0.1 度。 */
} bldc_protocol_value_t;

/** @brief 多圈绝对角度输入上限，单位为度。 */
#define BLDC_MAX_MULTI_TURN_ANGLE_DEG 1877385.2F

/** @brief 单圈绝对角度输入上限，单位为度。 */
#define BLDC_MAX_SINGLE_TURN_ANGLE_DEG 359.9F

/**
 * @brief  计算 F32C 协议的 BCC 校验值。
 * @param  data    从帧头开始的待校验字节序列。
 * @param  length  参与校验的字节数，不包含 BCC 和帧尾。
 * @return 返回所有输入字节逐字节异或后的校验值。
 */
static uint8_t calculate_bcc(const uint8_t *data, uint8_t length)
{
    uint8_t bcc;
    uint8_t index;

    bcc = 0U;
    for (index = 0U; index < length; index++) {
        bcc ^= data[index];
    }

    return bcc;
}

/**
 * @brief  检查电机实例及其所属总线是否可用于通信。
 * @param  motor  待检查的电机实例。
 * @return 实例、总线和串口句柄均有效时返回 1，否则返回 0。
 */
static uint8_t is_motor_valid(const bldc_motor_t *motor)
{
    return (motor != NULL) && (motor->bus != NULL) && (motor->bus->huart != NULL);
}

/**
 * @brief  计算发送环形队列当前可用的帧槽数量。
 * @param  bus  已初始化的共享串口总线实例。
 * @return 返回可继续写入的帧数量；始终保留一个槽用于区分队列空和队列满。
 */
static uint8_t tx_queue_free_count(const bldc_bus_t *bus)
{
    uint16_t used;

    if (bus->tx_write_pos >= bus->tx_read_pos) {
        used = bus->tx_write_pos - bus->tx_read_pos;
    } else {
        used = (uint16_t)(BLDC_TX_QUEUE_CAPACITY - bus->tx_read_pos + bus->tx_write_pos);
    }

    return (uint8_t)(BLDC_TX_QUEUE_CAPACITY - used - 1U);
}

/**
 * @brief  按 F32C 协议组装一帧发送数据。
 * @param  frame           调用方提供的输出帧对象。
 * @param  address         目标电机地址，范围为 1～255。
 * @param  function        协议功能码。
 * @param  payload         数据区首地址；数据长度为 0 时允许为空。
 * @param  payload_length  数据区字节数。
 * @return 组帧成功返回 BLDC_STATUS_OK；参数或帧长无效时返回参数错误。
 */
static bldc_status_t build_frame(bldc_tx_frame_t *frame, uint8_t address, uint8_t function,
    const uint8_t *payload, uint8_t payload_length)
{
    uint8_t index;
    uint8_t bcc_index;

    if ((frame == NULL) || (address == 0U) || ((payload_length > 0U) && (payload == NULL)) ||
        ((uint8_t)(payload_length + 5U) > BLDC_MAX_FRAME_SIZE)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }

    frame->data[0] = BLDC_FRAME_HEADER;
    frame->data[1] = address;
    frame->data[2] = function;
    for (index = 0U; index < payload_length; index++) {
        frame->data[index + 3U] = payload[index];
    }

    bcc_index = payload_length + 3U;
    frame->data[bcc_index] = calculate_bcc(frame->data, bcc_index);
    frame->data[bcc_index + 1U] = BLDC_FRAME_TAIL;
    frame->length = payload_length + 5U;

    return BLDC_STATUS_OK;
}

/**
 * @brief  将一组完整协议帧原子加入发送队列。
 * @param  bus          已初始化的共享串口总线实例。
 * @param  frames       待入队的连续帧数组。
 * @param  frame_count  待入队帧数量。
 * @return 全部入队成功返回 BLDC_STATUS_OK；参数错误或队列不足返回对应状态。
 * @note   函数先检查整组容量，再提交写指针，避免组合命令只入队一部分。
 */
static bldc_status_t enqueue_frames(bldc_bus_t *bus, const bldc_tx_frame_t *frames,
    uint8_t frame_count)
{
    uint8_t index;
    uint8_t write_pos;

    if ((bus == NULL) || (frames == NULL) || (frame_count == 0U)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }
    if (tx_queue_free_count(bus) < frame_count) {
        bus->last_status = BLDC_STATUS_QUEUE_FULL;
        return BLDC_STATUS_QUEUE_FULL;
    }

    write_pos = bus->tx_write_pos;
    for (index = 0U; index < frame_count; index++) {
        bus->tx_queue[write_pos] = frames[index];
        write_pos = (write_pos + 1U) % BLDC_TX_QUEUE_CAPACITY;
    }
    bus->tx_write_pos = write_pos;
    bus->last_status = BLDC_STATUS_OK;

    return BLDC_STATUS_OK;
}

/**
 * @brief  组装并入队一个不含数据区的设备命令。
 * @param  motor     已注册的目标电机实例。
 * @param  function  不带数据区的协议功能码。
 * @return 入队成功返回 BLDC_STATUS_OK；实例无效或队列满返回对应状态。
 */
static bldc_status_t enqueue_no_payload(bldc_motor_t *motor, uint8_t function)
{
    bldc_tx_frame_t frame;
    bldc_status_t status;

    if (!is_motor_valid(motor)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }

    status = build_frame(&frame, motor->address, function, NULL, 0U);
    if (status != BLDC_STATUS_OK) {
        return status;
    }

    return enqueue_frames(motor->bus, &frame, 1U);
}

/**
 * @brief  将无符号 16 位数编码为协议使用的大端字节序。
 * @param  payload  至少可写入 2 字节的输出缓冲区。
 * @param  value    待编码数值。
 * @return 无。
 */
static void encode_u16(uint8_t *payload, uint16_t value)
{
    payload[0] = (uint8_t)(value >> 8U);
    payload[1] = (uint8_t)value;
}

/**
 * @brief  将有符号 32 位数编码为大端二进制补码。
 * @param  payload  至少可写入 4 字节的输出缓冲区。
 * @param  value    待编码数值。
 * @return 无。
 */
static void encode_i32(uint8_t *payload, int32_t value)
{
    uint32_t encoded;

    encoded = (uint32_t)value;
    payload[0] = (uint8_t)(encoded >> 24U);
    payload[1] = (uint8_t)(encoded >> 16U);
    payload[2] = (uint8_t)(encoded >> 8U);
    payload[3] = (uint8_t)encoded;
}

/**
 * @brief  将无符号容器中的 32 位二进制补码解析为有符号数。
 * @param  value  按位保存的 32 位二进制补码。
 * @return 返回解析后的有符号数。
 * @note   显式处理符号位，避免依赖超范围无符号转有符号的实现定义行为。
 */
static int32_t decode_i32(uint32_t value)
{
    uint32_t magnitude;

    if ((value & 0x80000000UL) == 0U) {
        return (int32_t)value;
    }
    if (value == 0x80000000UL) {
        return INT32_MIN;
    }

    magnitude = (~value) + 1U;
    return -(int32_t)magnitude;
}

/**
 * @brief  将以度表示的角度转换为协议使用的 0.1 度定点数。
 * @param  angle_deg  输入角度，单位为度。
 * @return 返回放大 10 倍并四舍五入后的有符号整数。
 * @note   调用前必须完成角度范围和非数值检查。
 */
static int32_t angle_deg_to_x10(float angle_deg)
{
    float scaled_angle;

    scaled_angle = angle_deg * 10.0F;
    if (scaled_angle >= 0.0F) {
        return (int32_t)(scaled_angle + 0.5F);
    }

    return (int32_t)(scaled_angle - 0.5F);
}

/**
 * @brief  组装控制模式设置帧。
 * @param  frame  调用方提供的输出帧对象。
 * @param  motor  已注册的目标电机实例。
 * @param  mode   F32C 支持的控制模式。
 * @return 组帧成功返回 BLDC_STATUS_OK；模式无效时返回参数错误。
 */
static bldc_status_t build_mode_frame(bldc_tx_frame_t *frame, const bldc_motor_t *motor,
    bldc_mode_t mode)
{
    uint8_t payload[2];

    if ((mode < BLDC_MODE_SPEED) || (mode > BLDC_MODE_SINGLE_TURN_DIRECT)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }

    encode_u16(payload, (uint16_t)mode);
    return build_frame(frame, motor->address, BLDC_FUNCTION_MODE, payload, sizeof(payload));
}

/**
 * @brief  组装目标速度设置帧。
 * @param  frame           调用方提供的输出帧对象。
 * @param  motor           已注册的目标电机实例。
 * @param  speed_rpm       目标速度，单位为 RPM。
 * @param  allow_negative  非零时允许负速度，零时仅允许位置模式使用的非负速度。
 * @return 组帧成功返回 BLDC_STATUS_OK；速度超限或方向无效时返回参数错误。
 */
static bldc_status_t build_speed_frame(bldc_tx_frame_t *frame, const bldc_motor_t *motor,
    int16_t speed_rpm, uint8_t allow_negative)
{
    uint8_t payload[2];

    if ((speed_rpm > BLDC_MAX_SPEED_RPM) || (speed_rpm < -BLDC_MAX_SPEED_RPM) ||
        ((!allow_negative) && (speed_rpm < 0))) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }

    encode_u16(payload, (uint16_t)speed_rpm);
    return build_frame(frame, motor->address, BLDC_FUNCTION_SPEED, payload, sizeof(payload));
}

/**
 * @brief  组装 T 型轨迹加速度设置帧。
 * @param  frame              调用方提供的输出帧对象。
 * @param  motor              已注册的目标电机实例。
 * @param  acceleration_rps2 加速度，单位为转/秒^2。
 * @return 组帧成功返回 BLDC_STATUS_OK；参数无效时返回对应状态。
 */
static bldc_status_t build_acceleration_frame(bldc_tx_frame_t *frame, const bldc_motor_t *motor,
    uint16_t acceleration_rps2)
{
    uint8_t payload[2];

    encode_u16(payload, acceleration_rps2);
    return build_frame(frame, motor->address, BLDC_FUNCTION_ACCELERATION, payload, sizeof(payload));
}

/**
 * @brief  组装多圈绝对角度设置帧。
 * @param  frame      调用方提供的输出帧对象。
 * @param  motor      已注册的目标电机实例。
 * @param  angle_deg  多圈目标角度，单位为度。
 * @return 组帧成功返回 BLDC_STATUS_OK；角度越界或非数值时返回参数错误。
 */
static bldc_status_t build_multi_turn_frame(bldc_tx_frame_t *frame, const bldc_motor_t *motor,
    float angle_deg)
{
    uint8_t payload[4];
    int32_t angle_x10;

    if (!((angle_deg >= -BLDC_MAX_MULTI_TURN_ANGLE_DEG) &&
            (angle_deg <= BLDC_MAX_MULTI_TURN_ANGLE_DEG))) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }
    angle_x10 = angle_deg_to_x10(angle_deg);
    if ((angle_x10 > BLDC_MAX_MULTI_TURN_ANGLE_X10) ||
        (angle_x10 < -BLDC_MAX_MULTI_TURN_ANGLE_X10)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }

    encode_i32(payload, angle_x10);
    return build_frame(frame, motor->address, BLDC_FUNCTION_MULTI_TURN_ANGLE, payload,
        sizeof(payload));
}

/**
 * @brief  组装单圈绝对角度设置帧。
 * @param  frame      调用方提供的输出帧对象。
 * @param  motor      已注册的目标电机实例。
 * @param  angle_deg  单圈目标角度，范围为 0～359.9 度。
 * @return 组帧成功返回 BLDC_STATUS_OK；角度越界或非数值时返回参数错误。
 */
static bldc_status_t build_single_turn_frame(bldc_tx_frame_t *frame, const bldc_motor_t *motor,
    float angle_deg)
{
    uint8_t payload[2];
    int32_t angle_x10;

    if (!((angle_deg >= 0.0F) && (angle_deg <= BLDC_MAX_SINGLE_TURN_ANGLE_DEG))) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }
    angle_x10 = angle_deg_to_x10(angle_deg);
    if ((angle_x10 < 0) || (angle_x10 > BLDC_MAX_SINGLE_TURN_ANGLE_X10)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }

    encode_u16(payload, (uint16_t)angle_x10);
    return build_frame(frame, motor->address, BLDC_FUNCTION_SINGLE_TURN_ANGLE, payload,
        sizeof(payload));
}

/**
 * @brief  生成并原子入队一种完整控制模式所需的命令序列。
 * @param  motor              已注册的目标电机实例。
 * @param  mode               目标控制模式。
 * @param  angle_deg          位置模式目标角度，单位为度。
 * @param  speed_rpm          目标或过程最大速度，单位为 RPM。
 * @param  acceleration_rps2 T 型轨迹加速度，单位为转/秒^2。
 * @param  has_angle          非零时在序列末尾加入角度命令。
 * @param  has_acceleration   非零时在序列中加入加速度命令。
 * @return 整组命令入队成功返回 BLDC_STATUS_OK；否则不入队并返回对应状态。
 */
static bldc_status_t enqueue_control_sequence(bldc_motor_t *motor, bldc_mode_t mode,
    float angle_deg, int16_t speed_rpm, uint16_t acceleration_rps2, uint8_t has_angle,
    uint8_t has_acceleration)
{
    bldc_tx_frame_t frames[4];
    bldc_status_t status;
    uint8_t frame_count;

    if (!is_motor_valid(motor)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }

    frame_count = 0U;
    status = build_mode_frame(&frames[frame_count++], motor, mode);
    if (status != BLDC_STATUS_OK) {
        return status;
    }
    if (has_acceleration) {
        status = build_acceleration_frame(&frames[frame_count++], motor, acceleration_rps2);
        if (status != BLDC_STATUS_OK) {
            return status;
        }
    }
    status = build_speed_frame(&frames[frame_count++], motor, speed_rpm, mode == BLDC_MODE_SPEED);
    if (status != BLDC_STATUS_OK) {
        return status;
    }
    if (has_angle) {
        if ((mode == BLDC_MODE_MULTI_TURN_TRAPEZOIDAL) || (mode == BLDC_MODE_MULTI_TURN_DIRECT)) {
            status = build_multi_turn_frame(&frames[frame_count++], motor, angle_deg);
        } else {
            status = build_single_turn_frame(&frames[frame_count++], motor, angle_deg);
        }
        if (status != BLDC_STATUS_OK) {
            return status;
        }
    }

    return enqueue_frames(motor->bus, frames, frame_count);
}

/**
 * @brief  根据设备地址查找已注册的电机实例。
 * @param  bus      已初始化的共享串口总线实例。
 * @param  address  反馈帧携带的设备地址。
 * @return 找到时返回电机实例指针，否则返回 NULL。
 */
static bldc_motor_t *find_motor(bldc_bus_t *bus, uint8_t address)
{
    uint8_t index;

    for (index = 0U; index < bus->motor_count; index++) {
        if ((bus->motors[index] != NULL) && (bus->motors[index]->address == address)) {
            return bus->motors[index];
        }
    }

    return NULL;
}

/**
 * @brief  校验并解析一帧固定长度的 F32C 数据反馈。
 * @param  bus    已初始化的共享串口总线实例。
 * @param  frame  长度为 BLDC_FEEDBACK_FRAME_SIZE 的完整反馈帧。
 * @return 无。
 * @note   反馈负载为大端 32 位数，符号和单位缩放由反馈类型决定。
 */
static void parse_feedback_frame(bldc_bus_t *bus, const uint8_t *frame)
{
    bldc_motor_t *motor;
    bldc_feedback_type_t type;
    uint32_t raw_value;

    if ((frame[0] != BLDC_FRAME_HEADER) ||
        (frame[BLDC_FEEDBACK_FRAME_SIZE - 1U] != BLDC_FRAME_TAIL) ||
        (calculate_bcc(frame, BLDC_FEEDBACK_FRAME_SIZE - 2U) !=
            frame[BLDC_FEEDBACK_FRAME_SIZE - 2U])) {
        bus->last_status = BLDC_STATUS_IO_ERROR;
        return;
    }

    motor = find_motor(bus, frame[1]);
    type = (bldc_feedback_type_t)frame[2];
    if ((motor == NULL) || (type >= BLDC_FEEDBACK_COUNT)) {
        return;
    }

    raw_value = ((uint32_t)frame[3] << 24U) | ((uint32_t)frame[4] << 16U) |
                ((uint32_t)frame[5] << 8U) | (uint32_t)frame[6];

    switch (type) {
        case BLDC_FEEDBACK_SPEED:
            motor->telemetry.speed_rpm = decode_i32(raw_value);
            break;

        case BLDC_FEEDBACK_TOTAL_ANGLE:
            motor->telemetry.total_angle_deg = (float)decode_i32(raw_value) / 10.0F;
            break;

        case BLDC_FEEDBACK_MECHANICAL_ANGLE:
            motor->telemetry.mechanical_angle_deg = (float)raw_value / 10.0F;
            break;

        case BLDC_FEEDBACK_ACCELERATION:
            /* 手册反馈示例中原始值 200 表示 100 转/秒^2。 */
            motor->telemetry.acceleration_rps2 = (float)raw_value / 2.0F;
            break;

        case BLDC_FEEDBACK_BUS_VOLTAGE:
            motor->telemetry.bus_voltage_v = (float)raw_value / 100.0F;
            break;

        default:
            return;
    }

    motor->telemetry.valid_mask |= (uint32_t)1UL << (uint32_t)type;
    motor->telemetry.update_tick_ms[type] = HAL_GetTick();
    bus->last_status = BLDC_STATUS_OK;
}

/**
 * @brief  消费接收 FIFO 并按固定帧长组装反馈帧。
 * @param  bus  已初始化的共享串口总线实例。
 * @return 无。
 * @note   本函数只在主循环调用，DMA 回调只负责写入 FIFO。
 */
static void process_rx_fifo(bldc_bus_t *bus)
{
    uint8_t byte;

    while (bus->rx_read_pos != bus->rx_write_pos) {
        byte = bus->rx_fifo[bus->rx_read_pos];
        bus->rx_read_pos = (bus->rx_read_pos + 1U) % BLDC_RX_FIFO_CAPACITY;

        if (bus->rx_frame_index == 0U) {
            if (byte == BLDC_FRAME_HEADER) {
                bus->rx_frame[0] = byte;
                bus->rx_frame_index = 1U;
            }
            continue;
        }

        bus->rx_frame[bus->rx_frame_index++] = byte;
        if (bus->rx_frame_index == BLDC_FEEDBACK_FRAME_SIZE) {
            parse_feedback_frame(bus, bus->rx_frame);
            bus->rx_frame_index = 0U;
        }
    }
}

/**
 * @brief  在总线空闲且满足最小命令间隔时启动下一帧 DMA 发送。
 * @param  bus  已初始化的共享串口总线实例。
 * @return 无。
 * @note   成功启动后由 bldc_tx_callback() 提交发送队列读指针。
 */
static void start_next_transmission(bldc_bus_t *bus)
{
    bldc_tx_frame_t *frame;
    HAL_StatusTypeDef hal_status;

    if (bus->is_tx_busy || (bus->tx_read_pos == bus->tx_write_pos)) {
        return;
    }
    if ((uint32_t)(HAL_GetTick() - bus->last_tx_tick_ms) < BLDC_MIN_COMMAND_INTERVAL_MS) {
        return;
    }

    frame = &bus->tx_queue[bus->tx_read_pos];
    hal_status = HAL_UART_Transmit_DMA(bus->huart, frame->data, frame->length);
    if (hal_status == HAL_OK) {
        bus->is_tx_busy = 1U;
        bus->last_status = BLDC_STATUS_OK;
    } else if (hal_status == HAL_BUSY) {
        bus->last_status = BLDC_STATUS_BUSY;
    } else {
        bus->last_status = BLDC_STATUS_IO_ERROR;
    }
}

/**
 * @brief  初始化一条 F32C 共享串口总线并启动 DMA 空闲接收。
 * @param  bus    调用方持有的总线实例，成功后由驱动管理其内部状态。
 * @param  huart  已完成 115200、8N1 和 DMA 配置的 HAL 串口句柄。
 * @return 成功返回 BLDC_STATUS_OK；参数、忙或硬件错误返回对应状态。
 * @note   函数不初始化 HAL 外设，必须先完成 CubeMX 生成的串口和 DMA 初始化。
 */
bldc_status_t bldc_bus_init(bldc_bus_t *bus, UART_HandleTypeDef *huart)
{
    HAL_StatusTypeDef hal_status;

    if ((bus == NULL) || (huart == NULL)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }

    memset(bus, 0, sizeof(*bus));
    bus->huart = huart;
    bus->last_tx_tick_ms = HAL_GetTick() - BLDC_MIN_COMMAND_INTERVAL_MS;
    hal_status = HAL_UARTEx_ReceiveToIdle_DMA(huart, bus->dma_rx_buffer, BLDC_DMA_RX_BUFFER_SIZE);
    if (hal_status == HAL_BUSY) {
        bus->last_status = BLDC_STATUS_BUSY;
    } else if (hal_status != HAL_OK) {
        bus->last_status = BLDC_STATUS_IO_ERROR;
    } else {
        bus->last_status = BLDC_STATUS_OK;
    }

    return bus->last_status;
}

/**
 * @brief  在共享总线上注册一个电机实例。
 * @param  motor    调用方持有的电机实例，生命周期不得短于所属总线。
 * @param  bus      已通过 bldc_bus_init() 初始化的总线实例。
 * @param  address  设备地址，合法范围为 1～255，同一总线内不得重复。
 * @return 成功返回 BLDC_STATUS_OK；参数、地址重复或实例已满返回对应状态。
 * @note   本函数只建立软件实例，不会使能电机或发送控制命令。
 */
bldc_status_t bldc_motor_init(bldc_motor_t *motor, bldc_bus_t *bus, uint8_t address)
{
    uint8_t index;

    if ((motor == NULL) || (bus == NULL) || (bus->huart == NULL) || (address == 0U)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }
    if (bus->motor_count >= BLDC_MAX_MOTOR_COUNT) {
        return BLDC_STATUS_QUEUE_FULL;
    }
    for (index = 0U; index < bus->motor_count; index++) {
        if (bus->motors[index]->address == address) {
            return BLDC_STATUS_INVALID_ARGUMENT;
        }
    }

    memset(motor, 0, sizeof(*motor));
    motor->bus = bus;
    motor->address = address;
    bus->motors[bus->motor_count++] = motor;

    return BLDC_STATUS_OK;
}

/**
 * @brief  将电机使能命令加入发送队列。
 * @param  motor  已注册的电机实例。
 * @return 入队成功返回 BLDC_STATUS_OK；参数错误或队列满返回对应状态。
 */
bldc_status_t bldc_enable(bldc_motor_t *motor)
{
    return enqueue_no_payload(motor, BLDC_FUNCTION_ENABLE);
}

/**
 * @brief  将电机失能命令加入发送队列。
 * @param  motor  已注册的电机实例。
 * @return 入队成功返回 BLDC_STATUS_OK；参数错误或队列满返回对应状态。
 */
bldc_status_t bldc_disable(bldc_motor_t *motor)
{
    return enqueue_no_payload(motor, BLDC_FUNCTION_DISABLE);
}

/**
 * @brief  将控制模式设置命令加入发送队列。
 * @param  motor  已注册的电机实例。
 * @param  mode   F32C 支持的控制模式。
 * @return 入队成功返回 BLDC_STATUS_OK；模式无效或队列满返回对应状态。
 */
bldc_status_t bldc_set_mode(bldc_motor_t *motor, bldc_mode_t mode)
{
    bldc_tx_frame_t frame;
    bldc_status_t status;

    if (!is_motor_valid(motor)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }
    status = build_mode_frame(&frame, motor, mode);
    if (status != BLDC_STATUS_OK) {
        return status;
    }

    return enqueue_frames(motor->bus, &frame, 1U);
}

/**
 * @brief  将目标速度设置命令加入发送队列。
 * @param  motor      已注册的电机实例。
 * @param  speed_rpm  目标速度，速度模式范围为 -1000～1000 RPM。
 * @return 入队成功返回 BLDC_STATUS_OK；参数越界或队列满返回对应状态。
 */
bldc_status_t bldc_set_speed(bldc_motor_t *motor, int16_t speed_rpm)
{
    bldc_tx_frame_t frame;
    bldc_status_t status;

    if (!is_motor_valid(motor)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }
    status = build_speed_frame(&frame, motor, speed_rpm, 1U);
    if (status != BLDC_STATUS_OK) {
        return status;
    }

    return enqueue_frames(motor->bus, &frame, 1U);
}

/**
 * @brief  将 T 型轨迹加速度设置命令加入发送队列。
 * @param  motor              已注册的电机实例。
 * @param  acceleration_rps2 加速度，单位为转/秒^2，协议范围为 0～65535。
 * @return 入队成功返回 BLDC_STATUS_OK；参数错误或队列满返回对应状态。
 */
bldc_status_t bldc_set_acceleration(bldc_motor_t *motor, uint16_t acceleration_rps2)
{
    bldc_tx_frame_t frame;
    bldc_status_t status;

    if (!is_motor_valid(motor)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }
    status = build_acceleration_frame(&frame, motor, acceleration_rps2);
    if (status != BLDC_STATUS_OK) {
        return status;
    }

    return enqueue_frames(motor->bus, &frame, 1U);
}

/**
 * @brief  将多圈绝对角度设置命令加入发送队列。
 * @param  motor      已注册的电机实例。
 * @param  angle_deg  目标角度，单位为度，协议分辨率为 0.1 度。
 * @return 入队成功返回 BLDC_STATUS_OK；角度越界或队列满返回对应状态。
 */
bldc_status_t bldc_set_multi_turn_angle(bldc_motor_t *motor, float angle_deg)
{
    bldc_tx_frame_t frame;
    bldc_status_t status;

    if (!is_motor_valid(motor)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }
    status = build_multi_turn_frame(&frame, motor, angle_deg);
    if (status != BLDC_STATUS_OK) {
        return status;
    }

    return enqueue_frames(motor->bus, &frame, 1U);
}

/**
 * @brief  将单圈绝对角度设置命令加入发送队列。
 * @param  motor      已注册的电机实例。
 * @param  angle_deg  目标角度，范围为 0～359.9 度，协议分辨率为 0.1 度。
 * @return 入队成功返回 BLDC_STATUS_OK；角度越界或队列满返回对应状态。
 */
bldc_status_t bldc_set_single_turn_angle(bldc_motor_t *motor, float angle_deg)
{
    bldc_tx_frame_t frame;
    bldc_status_t status;

    if (!is_motor_valid(motor)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }
    status = build_single_turn_frame(&frame, motor, angle_deg);
    if (status != BLDC_STATUS_OK) {
        return status;
    }

    return enqueue_frames(motor->bus, &frame, 1U);
}

/**
 * @brief  原子加入速度模式、加速度和目标速度命令。
 * @param  motor              已注册的电机实例。
 * @param  speed_rpm          目标速度，范围为 -1000～1000 RPM。
 * @param  acceleration_rps2 加速度，单位为转/秒^2。
 * @return 全部命令入队成功返回 BLDC_STATUS_OK；否则不入队并返回对应状态。
 * @note   调用前需使用 bldc_enable() 使能电机。
 */
bldc_status_t bldc_run_speed(bldc_motor_t *motor, int16_t speed_rpm, uint16_t acceleration_rps2)
{
    return enqueue_control_sequence(motor, BLDC_MODE_SPEED, 0.0F, speed_rpm, acceleration_rps2, 0U,
        1U);
}

/**
 * @brief  原子加入多圈 T 型位置模式、加速度、速度和目标角度命令。
 * @param  motor              已注册的电机实例。
 * @param  angle_deg          多圈目标角度，单位为度。
 * @param  speed_rpm          位置过程最大速度，范围为 0～1000 RPM。
 * @param  acceleration_rps2 T 型轨迹加速度，单位为转/秒^2。
 * @return 全部命令入队成功返回 BLDC_STATUS_OK；否则不入队并返回对应状态。
 * @note   调用前需使用 bldc_enable() 使能电机。
 */
bldc_status_t bldc_move_multi_turn_trapezoidal(bldc_motor_t *motor, float angle_deg,
    uint16_t speed_rpm, uint16_t acceleration_rps2)
{
    return enqueue_control_sequence(motor, BLDC_MODE_MULTI_TURN_TRAPEZOIDAL, angle_deg,
        (int16_t)speed_rpm, acceleration_rps2, 1U, 1U);
}

/**
 * @brief  原子加入单圈 T 型位置模式、加速度、速度和目标角度命令。
 * @param  motor              已注册的电机实例。
 * @param  angle_deg          单圈目标角度，范围为 0～359.9 度。
 * @param  speed_rpm          位置过程最大速度，范围为 0～1000 RPM。
 * @param  acceleration_rps2 T 型轨迹加速度，单位为转/秒^2。
 * @return 全部命令入队成功返回 BLDC_STATUS_OK；否则不入队并返回对应状态。
 * @note   调用前需使用 bldc_enable() 使能电机。
 */
bldc_status_t bldc_move_single_turn_trapezoidal(bldc_motor_t *motor, float angle_deg,
    uint16_t speed_rpm, uint16_t acceleration_rps2)
{
    return enqueue_control_sequence(motor, BLDC_MODE_SINGLE_TURN_TRAPEZOIDAL, angle_deg,
        (int16_t)speed_rpm, acceleration_rps2, 1U, 1U);
}

/**
 * @brief  原子加入多圈直通位置模式、速度和目标角度命令。
 * @param  motor      已注册的电机实例。
 * @param  angle_deg  多圈目标角度，单位为度。
 * @param  speed_rpm  位置过程最大速度，范围为 0～1000 RPM。
 * @return 全部命令入队成功返回 BLDC_STATUS_OK；否则不入队并返回对应状态。
 * @note   直通模式不使用加速度参数；调用前需使用 bldc_enable() 使能电机。
 */
bldc_status_t bldc_move_multi_turn_direct(bldc_motor_t *motor, float angle_deg, uint16_t speed_rpm)
{
    return enqueue_control_sequence(motor, BLDC_MODE_MULTI_TURN_DIRECT, angle_deg,
        (int16_t)speed_rpm, 0U, 1U, 0U);
}

/**
 * @brief  原子加入单圈直通位置模式、速度和目标角度命令。
 * @param  motor      已注册的电机实例。
 * @param  angle_deg  单圈目标角度，范围为 0～359.9 度。
 * @param  speed_rpm  位置过程最大速度，范围为 0～1000 RPM。
 * @return 全部命令入队成功返回 BLDC_STATUS_OK；否则不入队并返回对应状态。
 * @note   直通模式不使用加速度参数；调用前需使用 bldc_enable() 使能电机。
 */
bldc_status_t bldc_move_single_turn_direct(bldc_motor_t *motor, float angle_deg, uint16_t speed_rpm)
{
    return enqueue_control_sequence(motor, BLDC_MODE_SINGLE_TURN_DIRECT, angle_deg,
        (int16_t)speed_rpm, 0U, 1U, 0U);
}

/**
 * @brief  将设备参数掉电保存命令加入发送队列。
 * @param  motor  已注册的电机实例。
 * @return 入队成功返回 BLDC_STATUS_OK；参数错误或队列满返回对应状态。
 */
bldc_status_t bldc_save_parameters(bldc_motor_t *motor)
{
    return enqueue_no_payload(motor, BLDC_FUNCTION_SAVE_PARAMETERS);
}

/**
 * @brief  将上电后累计角度清零命令加入发送队列。
 * @param  motor  已注册的电机实例。
 * @return 入队成功返回 BLDC_STATUS_OK；参数错误或队列满返回对应状态。
 */
bldc_status_t bldc_clear_total_angle(bldc_motor_t *motor)
{
    return enqueue_no_payload(motor, BLDC_FUNCTION_CLEAR_TOTAL_ANGLE);
}

/**
 * @brief  将当前位置设为单圈机械零点的命令加入发送队列。
 * @param  motor  已注册的电机实例。
 * @return 入队成功返回 BLDC_STATUS_OK；参数错误或队列满返回对应状态。
 * @note   F32C 会掉电保存机械零点，调用前应确认机构处于安全位置。
 */
bldc_status_t bldc_set_mechanical_zero(bldc_motor_t *motor)
{
    return enqueue_no_payload(motor, BLDC_FUNCTION_SET_MECHANICAL_ZERO);
}

/**
 * @brief  异步请求一个反馈字段。
 * @param  motor  已注册的电机实例。
 * @param  type   需要读取的反馈类型。
 * @return 请求入队成功返回 BLDC_STATUS_OK；参数错误或队列满返回对应状态。
 * @note   函数不等待设备响应；随后调用对应 bldc_get_*() 接口读取缓存结果。
 */
bldc_status_t bldc_request_feedback(bldc_motor_t *motor, bldc_feedback_type_t type)
{
    bldc_tx_frame_t frame;
    bldc_status_t status;
    uint8_t payload;

    if (!is_motor_valid(motor) || (type >= BLDC_FEEDBACK_COUNT)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }

    payload = (uint8_t)type;
    status = build_frame(&frame, motor->address, BLDC_FUNCTION_REQUEST_FEEDBACK, &payload,
        sizeof(payload));
    if (status != BLDC_STATUS_OK) {
        return status;
    }

    return enqueue_frames(motor->bus, &frame, 1U);
}

/**
 * @brief  原子加入速度、累计角度、机械角度、加速度和母线电压查询。
 * @param  motor  已注册的电机实例。
 * @return 五个请求全部入队成功返回 BLDC_STATUS_OK；否则不入队并返回对应状态。
 * @note   查询结果由 bldc_task() 解析，字段可能在不同时间更新。
 */
bldc_status_t bldc_request_all_feedback(bldc_motor_t *motor)
{
    bldc_tx_frame_t frames[BLDC_FEEDBACK_COUNT];
    bldc_status_t status;
    uint8_t payload;

    if (!is_motor_valid(motor)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }

    for (payload = 0U; payload < BLDC_FEEDBACK_COUNT; payload++) {
        status = build_frame(&frames[payload], motor->address, BLDC_FUNCTION_REQUEST_FEEDBACK,
            &payload, sizeof(payload));
        if (status != BLDC_STATUS_OK) {
            return status;
        }
    }

    return enqueue_frames(motor->bus, frames, BLDC_FEEDBACK_COUNT);
}

/**
 * @brief  复制电机当前反馈快照。
 * @param  motor      已注册的电机实例。
 * @param  telemetry  调用方提供的输出对象。
 * @return 至少一个字段有效时返回 BLDC_STATUS_OK；尚无数据时返回 NOT_READY。
 * @note   调用方必须通过 valid_mask 判断所需字段是否已经更新。
 */
bldc_status_t bldc_get_telemetry(const bldc_motor_t *motor, bldc_telemetry_t *telemetry)
{
    if (!is_motor_valid(motor) || (telemetry == NULL)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }
    if (motor->telemetry.valid_mask == 0U) {
        return BLDC_STATUS_NOT_READY;
    }

    *telemetry = motor->telemetry;
    return BLDC_STATUS_OK;
}

/**
 * @brief  读取缓存的速度反馈。
 * @param  motor      已注册的电机实例。
 * @param  speed_rpm  输出速度，单位为 RPM。
 * @return 字段有效时返回 BLDC_STATUS_OK；尚无数据时返回 NOT_READY。
 */
bldc_status_t bldc_get_speed(const bldc_motor_t *motor, int32_t *speed_rpm)
{
    if (!is_motor_valid(motor) || (speed_rpm == NULL)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }
    if ((motor->telemetry.valid_mask & BLDC_FEEDBACK_VALID_SPEED) == 0U) {
        return BLDC_STATUS_NOT_READY;
    }

    *speed_rpm = motor->telemetry.speed_rpm;
    return BLDC_STATUS_OK;
}

/**
 * @brief  读取缓存的上电后累计角度。
 * @param  motor      已注册的电机实例。
 * @param  angle_deg  输出累计角度，单位为度。
 * @return 字段有效时返回 BLDC_STATUS_OK；尚无数据时返回 NOT_READY。
 */
bldc_status_t bldc_get_total_angle(const bldc_motor_t *motor, float *angle_deg)
{
    if (!is_motor_valid(motor) || (angle_deg == NULL)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }
    if ((motor->telemetry.valid_mask & BLDC_FEEDBACK_VALID_TOTAL_ANGLE) == 0U) {
        return BLDC_STATUS_NOT_READY;
    }

    *angle_deg = motor->telemetry.total_angle_deg;
    return BLDC_STATUS_OK;
}

/**
 * @brief  读取缓存的单圈机械角度。
 * @param  motor      已注册的电机实例。
 * @param  angle_deg  输出机械角度，单位为度。
 * @return 字段有效时返回 BLDC_STATUS_OK；尚无数据时返回 NOT_READY。
 */
bldc_status_t bldc_get_mechanical_angle(const bldc_motor_t *motor, float *angle_deg)
{
    if (!is_motor_valid(motor) || (angle_deg == NULL)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }
    if ((motor->telemetry.valid_mask & BLDC_FEEDBACK_VALID_MECHANICAL_ANGLE) == 0U) {
        return BLDC_STATUS_NOT_READY;
    }

    *angle_deg = motor->telemetry.mechanical_angle_deg;
    return BLDC_STATUS_OK;
}

/**
 * @brief  读取缓存的加速度反馈。
 * @param  motor              已注册的电机实例。
 * @param  acceleration_rps2 输出加速度，单位为转/秒^2。
 * @return 字段有效时返回 BLDC_STATUS_OK；尚无数据时返回 NOT_READY。
 */
bldc_status_t bldc_get_acceleration(const bldc_motor_t *motor, float *acceleration_rps2)
{
    if (!is_motor_valid(motor) || (acceleration_rps2 == NULL)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }
    if ((motor->telemetry.valid_mask & BLDC_FEEDBACK_VALID_ACCELERATION) == 0U) {
        return BLDC_STATUS_NOT_READY;
    }

    *acceleration_rps2 = motor->telemetry.acceleration_rps2;
    return BLDC_STATUS_OK;
}

/**
 * @brief  读取缓存的母线电压反馈。
 * @param  motor     已注册的电机实例。
 * @param  voltage_v 输出母线电压，单位为伏特。
 * @return 字段有效时返回 BLDC_STATUS_OK；尚无数据时返回 NOT_READY。
 */
bldc_status_t bldc_get_bus_voltage(const bldc_motor_t *motor, float *voltage_v)
{
    if (!is_motor_valid(motor) || (voltage_v == NULL)) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }
    if ((motor->telemetry.valid_mask & BLDC_FEEDBACK_VALID_BUS_VOLTAGE) == 0U) {
        return BLDC_STATUS_NOT_READY;
    }

    *voltage_v = motor->telemetry.bus_voltage_v;
    return BLDC_STATUS_OK;
}

/**
 * @brief  获取总线最近一次通信状态。
 * @param  bus  已初始化的总线实例。
 * @return 返回最近状态；bus 为空时返回 BLDC_STATUS_INVALID_ARGUMENT。
 */
bldc_status_t bldc_get_last_status(const bldc_bus_t *bus)
{
    if (bus == NULL) {
        return BLDC_STATUS_INVALID_ARGUMENT;
    }

    return bus->last_status;
}

/**
 * @brief  推进反馈解析与发送队列一次。
 * @param  bus  已初始化的总线实例。
 * @return 无。
 * @note   本函数非阻塞，必须在主循环中持续调用；相邻命令间隔至少为 1 ms。
 */
void bldc_task(bldc_bus_t *bus)
{
    if ((bus == NULL) || (bus->huart == NULL)) {
        return;
    }

    process_rx_fifo(bus);
    start_next_transmission(bus);
}

/**
 * @brief  接收 USART DMA 空闲事件并重新启动下一次 DMA 接收。
 * @param  bus    已初始化的总线实例。
 * @param  huart  触发回调的 HAL 串口句柄。
 * @param  size   本次 DMA 实际收到的字节数。
 * @return 无。
 * @note   仅允许从 HAL_UARTEx_RxEventCallback() 中调用；函数只搬运数据。
 */
void bldc_rx_callback(bldc_bus_t *bus, UART_HandleTypeDef *huart, uint16_t size)
{
    HAL_StatusTypeDef hal_status;
    uint16_t index;
    uint16_t next_write_pos;

    if ((bus == NULL) || (huart == NULL) || (bus->huart == NULL) ||
        (huart->Instance != bus->huart->Instance)) {
        return;
    }

    if (size > BLDC_DMA_RX_BUFFER_SIZE) {
        size = BLDC_DMA_RX_BUFFER_SIZE;
        bus->last_status = BLDC_STATUS_IO_ERROR;
    }
    for (index = 0U; index < size; index++) {
        next_write_pos = (bus->rx_write_pos + 1U) % BLDC_RX_FIFO_CAPACITY;
        if (next_write_pos == bus->rx_read_pos) {
            bus->last_status = BLDC_STATUS_QUEUE_FULL;
            break;
        }
        bus->rx_fifo[bus->rx_write_pos] = bus->dma_rx_buffer[index];
        bus->rx_write_pos = next_write_pos;
    }

    memset(bus->dma_rx_buffer, 0, sizeof(bus->dma_rx_buffer));
    hal_status =
        HAL_UARTEx_ReceiveToIdle_DMA(bus->huart, bus->dma_rx_buffer, BLDC_DMA_RX_BUFFER_SIZE);
    if (hal_status == HAL_BUSY) {
        bus->last_status = BLDC_STATUS_BUSY;
    } else if (hal_status != HAL_OK) {
        bus->last_status = BLDC_STATUS_IO_ERROR;
    }
}

/**
 * @brief  处理 USART DMA 发送完成事件。
 * @param  bus    已初始化的总线实例。
 * @param  huart  触发回调的 HAL 串口句柄。
 * @return 无。
 * @note   仅允许从 HAL_UART_TxCpltCallback() 中调用，不在 ISR 中发送下一帧。
 */
void bldc_tx_callback(bldc_bus_t *bus, UART_HandleTypeDef *huart)
{
    if ((bus == NULL) || (huart == NULL) || (bus->huart == NULL) ||
        (huart->Instance != bus->huart->Instance) || !bus->is_tx_busy) {
        return;
    }

    bus->tx_read_pos = (bus->tx_read_pos + 1U) % BLDC_TX_QUEUE_CAPACITY;
    bus->is_tx_busy = 0U;
    bus->last_tx_tick_ms = HAL_GetTick();
    bus->last_status = BLDC_STATUS_OK;
}

/**
 * @brief  处理 USART 错误事件并恢复 DMA 空闲接收。
 * @param  bus    已初始化的总线实例。
 * @param  huart  触发回调的 HAL 串口句柄。
 * @return 无。
 * @note   仅允许从 HAL_UART_ErrorCallback() 中调用；当前帧将保留并重试。
 */
void bldc_error_callback(bldc_bus_t *bus, UART_HandleTypeDef *huart)
{
    HAL_StatusTypeDef hal_status;

    if ((bus == NULL) || (huart == NULL) || (bus->huart == NULL) ||
        (huart->Instance != bus->huart->Instance)) {
        return;
    }

    bus->rx_read_pos = 0U;
    bus->rx_write_pos = 0U;
    bus->rx_frame_index = 0U;
    bus->is_tx_busy = 0U;
    bus->last_status = BLDC_STATUS_IO_ERROR;
    memset(bus->dma_rx_buffer, 0, sizeof(bus->dma_rx_buffer));
    hal_status =
        HAL_UARTEx_ReceiveToIdle_DMA(bus->huart, bus->dma_rx_buffer, BLDC_DMA_RX_BUFFER_SIZE);
    if (hal_status == HAL_BUSY) {
        bus->last_status = BLDC_STATUS_BUSY;
    }
}
