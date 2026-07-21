/**
 * @file    gyroscope.c
 * @brief   姿态传感器串口通信模块（DMA + 环形缓冲区 + 帧解析 + yaw 增量）
 * @author  mizuniuo01
 * @date    2026-05-25
 * @version 1.0.0
 * @note    依赖：UART + DMA 外设已通过 CubeMX 配置
 * @note    STM32 使用硬件 IDLE 中断检测帧结束
 * @note    帧协议：0x55 + 0x53 + 8字节数据 + 校验和（共11字节）
 * @note    提供 yaw 增量追踪，配合摄像头帧同步
 * @warning ISR 回调中只做数据搬运，浮点转换在 gyro_task 中完成
 * @note    参数非法时通过 error_report(ERROR_SOURCE_GYRO, DRV_ERR_PARAM) 上报
 *
 * @usage
 * ─────────────────────────────────────────────────────────
 * UART DMA + 环形缓冲区 + 三态帧解析 + yaw 增量计算。
 *
 * ── 初始化 ──
 *
 * gyro_init(&huart1);
 *
 * ── HAL 回调 ──
 *
 * void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t s)
 * { gyro_rx_callback(huart, s); }
 * void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
 * { gyro_error_callback(huart); }
 *
 * ── ISR 中置 gyro_tick_flag（用于 yaw 增量更新）──
 *
 * ── 主循环 ──
 *
 * gyro_task();  // 帧解析 + yaw 增量
 *
 * ── 摄像头帧同步 ──
 * 可用于使用陀螺仪数据预测视觉延迟
 * gyro_on_camera_frame();  // 锁定当前增量
 * float dyaw = gyro_get_delta_yaw();
 */

#include "gyroscope.h"
#include "error_handler.h"
#include <string.h>

static gyro_handle_t gyro_inst;
static gyro_data_t gyro_data;
static uint8_t gyro_data_valid;

volatile uint8_t gyro_tick_flag;

/* yaw 增量追踪 */
static float gyro_delta_accumulated;
static float gyro_frame_delta_yaw;
static float gyro_last_yaw;
static uint8_t gyro_delta_initialized;

/**
 * @brief  姿态传感器模块初始化
 * @param  huart  绑定的串口句柄指针
 * @retval 无
 */
void gyro_init(UART_HandleTypeDef *huart)
{
    HAL_StatusTypeDef status;

    if (!huart) {
        error_report(ERROR_SOURCE_GYRO, DRV_ERR_PARAM);
        return;
    }

    gyro_inst.huart = huart;
    gyro_inst.rx_read_pos = 0;
    gyro_inst.rx_write_pos = 0;
    gyro_inst.rx_state = GYRO_STATE_WAIT_HEADER;
    gyro_inst.frame_index = 0;

    gyro_delta_accumulated = 0.0f;
    gyro_frame_delta_yaw = 0.0f;
    gyro_last_yaw = 0.0f;
    gyro_delta_initialized = 0;
    gyro_data_valid = 0U;

    status = HAL_UARTEx_ReceiveToIdle_DMA(gyro_inst.huart, gyro_inst.dma_rx_buffer,
        GYRO_DMA_RX_BUF_SIZE);
    if (status == HAL_BUSY) {
        error_report(ERROR_SOURCE_GYRO, DRV_ERR_BUSY);
    } else if (status != HAL_OK) {
        error_report(ERROR_SOURCE_GYRO, DRV_ERR_IO);
    }
}

/**
 * @brief  DMA 接收完成回调（ISR 中调用），将数据推入 RX FIFO
 * @param  huart  触发中断的串口句柄
 * @param  size   实际接收数据长度
 * @retval 无
 */
void gyro_rx_callback(UART_HandleTypeDef *huart, uint16_t size)
{
    HAL_StatusTypeDef status;
    uint16_t next;
    uint16_t i;

    if (!huart || !gyro_inst.huart) {
        error_report(ERROR_SOURCE_GYRO, DRV_ERR_PARAM);
        return;
    }

    if (huart->Instance != gyro_inst.huart->Instance) {
        error_report(ERROR_SOURCE_GYRO, DRV_ERR_PARAM);
        return;
    }

    if (size > 0) {
        for (i = 0; i < size; i++) {
            next = (gyro_inst.rx_write_pos + 1) % GYRO_RX_FIFO_SIZE;
            if (next != gyro_inst.rx_read_pos) {
                gyro_inst.rx_fifo[gyro_inst.rx_write_pos] = gyro_inst.dma_rx_buffer[i];
                gyro_inst.rx_write_pos = next;
            }
        }
    }

    memset(gyro_inst.dma_rx_buffer, 0, GYRO_DMA_RX_BUF_SIZE);
    status = HAL_UARTEx_ReceiveToIdle_DMA(gyro_inst.huart, gyro_inst.dma_rx_buffer,
        GYRO_DMA_RX_BUF_SIZE);
    if (status == HAL_BUSY) {
        error_report(ERROR_SOURCE_GYRO, DRV_ERR_BUSY);
    } else if (status != HAL_OK) {
        error_report(ERROR_SOURCE_GYRO, DRV_ERR_IO);
    }
}

/**
 * @brief  串口错误回调（ISR 中调用），复位接收状态机
 * @param  huart  触发中断的串口句柄
 * @retval 无
 */
void gyro_error_callback(UART_HandleTypeDef *huart)
{
    HAL_StatusTypeDef status;

    if (!huart || !gyro_inst.huart) {
        error_report(ERROR_SOURCE_GYRO, DRV_ERR_PARAM);
        return;
    }

    if (huart->Instance != gyro_inst.huart->Instance) {
        error_report(ERROR_SOURCE_GYRO, DRV_ERR_PARAM);
        return;
    }

    error_report(ERROR_SOURCE_GYRO, DRV_ERR_IO);
    gyro_inst.rx_read_pos = 0;
    gyro_inst.rx_write_pos = 0;
    gyro_inst.rx_state = GYRO_STATE_WAIT_HEADER;
    gyro_inst.frame_index = 0;
    gyro_delta_initialized = 0;
    gyro_delta_accumulated = 0.0f;
    gyro_frame_delta_yaw = 0.0f;
    gyro_data_valid = 0U;

    memset(gyro_inst.dma_rx_buffer, 0, GYRO_DMA_RX_BUF_SIZE);
    status = HAL_UARTEx_ReceiveToIdle_DMA(gyro_inst.huart, gyro_inst.dma_rx_buffer,
        GYRO_DMA_RX_BUF_SIZE);
    if (status == HAL_BUSY) {
        error_report(ERROR_SOURCE_GYRO, DRV_ERR_BUSY);
    } else if (status != HAL_OK) {
        error_report(ERROR_SOURCE_GYRO, DRV_ERR_IO);
    }
}

/**
 * @brief  校验和验证
 * @param  buf  数据缓冲区
 * @param  len  数据长度（含校验字节）
 * @retval 0=校验失败，1=校验通过
 */
static uint8_t verify_checksum(const uint8_t *buf, uint8_t len)
{
    uint8_t i;
    uint8_t sum;

    sum = 0;
    for (i = 0; i < len - 1; i++) {
        sum += buf[i];
    }

    return (sum == buf[len - 1]) ? 1 : 0;
}

/**
 * @brief  解析角度数据帧并转换为度
 * @param  buf  11 字节原始数据帧
 * @retval 无
 */
static void parse_angle_frame(const uint8_t *buf)
{
    int16_t roll_raw;
    int16_t pitch_raw;
    int16_t yaw_raw;

    roll_raw = (int16_t)((buf[3] << 8) | buf[2]);
    pitch_raw = (int16_t)((buf[5] << 8) | buf[4]);
    yaw_raw = (int16_t)((buf[7] << 8) | buf[6]);

    gyro_data.roll = (float)roll_raw / 32768.0f * 180.0f;
    gyro_data.pitch = (float)pitch_raw / 32768.0f * 180.0f;
    gyro_data.yaw = (float)yaw_raw / 32768.0f * 180.0f;
    gyro_data_valid = 1U;
}

/**
 * @brief  三态帧解析
 * @note   帧结构：0x55 → 0x53 → 8字节数据+校验
 * @param  无
 * @retval 无
 */
static void parse_frames(void)
{
    uint8_t byte;

    while (gyro_inst.rx_read_pos != gyro_inst.rx_write_pos) {
        byte = gyro_inst.rx_fifo[gyro_inst.rx_read_pos];
        gyro_inst.rx_read_pos = (gyro_inst.rx_read_pos + 1) % GYRO_RX_FIFO_SIZE;

        switch (gyro_inst.rx_state) {
            case GYRO_STATE_WAIT_HEADER:
                if (byte == GYRO_FRAME_HEADER) {
                    gyro_inst.frame_buffer[0] = byte;
                    gyro_inst.frame_index = 1;
                    gyro_inst.rx_state = GYRO_STATE_WAIT_TYPE;
                }
                break;

            case GYRO_STATE_WAIT_TYPE:
                if (byte == GYRO_TYPE_ANGLE) {
                    gyro_inst.frame_buffer[1] = byte;
                    gyro_inst.frame_index = 2;
                    gyro_inst.rx_state = GYRO_STATE_RECEIVING_DATA;
                } else if (byte == GYRO_FRAME_HEADER) {
                    gyro_inst.frame_buffer[0] = byte;
                    gyro_inst.frame_index = 1;
                } else {
                    gyro_inst.rx_state = GYRO_STATE_WAIT_HEADER;
                }
                break;

            case GYRO_STATE_RECEIVING_DATA:
                gyro_inst.frame_buffer[gyro_inst.frame_index++] = byte;

                if (gyro_inst.frame_index >= GYRO_FRAME_LEN) {
                    if (verify_checksum(gyro_inst.frame_buffer, GYRO_FRAME_LEN)) {
                        parse_angle_frame(gyro_inst.frame_buffer);
                    }

                    gyro_inst.rx_state = GYRO_STATE_WAIT_HEADER;
                }
                break;

            default:
                gyro_inst.rx_state = GYRO_STATE_WAIT_HEADER;
                break;
        }
    }
}

/**
 * @brief  更新 yaw 增量（由 gyro_tick_flag 触发）
 * @note   处理角度 ±180° 跨越和异常跳变限幅
 * @param  无
 * @retval 无
 */
static void update_delta_yaw(void)
{
    float current_yaw;
    float delta;

    if (!gyro_delta_initialized) {
        gyro_last_yaw = gyro_data.yaw;
        gyro_delta_accumulated = 0.0f;
        gyro_frame_delta_yaw = 0.0f;
        gyro_delta_initialized = 1;
        return;
    }

    current_yaw = gyro_data.yaw;
    delta = current_yaw - gyro_last_yaw;

    /* 处理 ±180° 跨越 */
    if (delta > 180.0f) {
        delta -= 360.0f;
    } else if (delta < -180.0f) {
        delta += 360.0f;
    }

    /* 异常跳变限幅 */
    if (delta > GYRO_MAX_DELTA_DEG) {
        delta = GYRO_MAX_DELTA_DEG;
    } else if (delta < -GYRO_MAX_DELTA_DEG) {
        delta = -GYRO_MAX_DELTA_DEG;
    }

    gyro_delta_accumulated += delta;
    gyro_last_yaw = current_yaw;
}

/**
 * @brief  姿态传感器周期性任务（主循环中调用）
 * @note   帧解析 + yaw 增量更新
 * @param  无
 * @retval 无
 */
void gyro_task(void)
{
    parse_frames();

    if (gyro_tick_flag) {
        gyro_tick_flag = 0;
        update_delta_yaw();
    }
}

/**
 * @brief  摄像头帧到达时同步 yaw 增量
 * @note   将累积的 yaw 增量锁定到当前帧，并清零累积器
 * @param  无
 * @retval 无
 */
void gyro_on_camera_frame(void)
{
    gyro_frame_delta_yaw = gyro_delta_accumulated;
    gyro_delta_accumulated = 0.0f;
}

/**
 * @brief  获取当前帧的 yaw 增量
 * @param  无
 * @retval yaw 增量（度）
 */
float gyro_get_delta_yaw(void)
{
    return gyro_frame_delta_yaw;
}

/**
 * @brief  获取姿态传感器数据
 * @param  无
 * @retval 姿态角数据结构体（roll/pitch/yaw，度）
 */
gyro_data_t gyro_get_data(void)
{
    return gyro_data;
}

/**
 * @brief  判断是否已经解析到至少一帧合法姿态数据。
 * @return 已有合法数据返回 1，否则返回 0。
 */
uint8_t gyro_is_data_valid(void)
{
    return gyro_data_valid;
}
