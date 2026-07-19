/**
 * @file    cam.c
 * @brief   摄像头串口通信模块（DMA + 环形缓冲区 + 帧解析）
 * @author  mizuniuo01
 * @date    2026-05-25
 * @version 1.0.0
 * @note    依赖：UART + DMA 外设已通过 CubeMX 配置
 * @note    STM32 使用硬件 IDLE 中断检测帧结束
 * @note    帧协议：0xFF 帧头 + 数据 + 0xFE 帧尾
 * @warning ISR 回调中只做数据搬运，复杂逻辑在 cam_task 中处理
 * @note    参数非法时通过 error_report(ERROR_SOURCE_CAM, DRV_ERR_PARAM) 上报
 *
 * @usage
 * ─────────────────────────────────────────────────────────
 * UART DMA + 环形缓冲区 + 帧解析。
 * 当前为单实例设计（模块内部 static handle）。
 *
 * ── 初始化 ──
 *
 * cam_init(&huart1);
 *
 * ── HAL 回调（重写 HAL 弱函数）──
 *
 * void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
 * {
 *     cam_rx_callback(huart, size);
 * }
 *
 * ── 主循环 ──
 *
 * cam_task();  // 帧解析
 *
 * ── 数据读取 ──
 *
 * cam_data_t data = cam_get_data();
 */

#include "cam.h"
#include "error_handler.h"
#include <string.h>

static cam_handle_t cam_inst;
static cam_data_t cam_data;

/* 帧就绪标志位（ISR/task 置1，外部轮询清零） */
volatile uint8_t cam_frame_ready;

/**
 * @brief  摄像头模块初始化
 * @param  huart  绑定的串口句柄指针
 * @retval 无
 */
void cam_init(UART_HandleTypeDef *huart)
{
    HAL_StatusTypeDef status;

    if (!huart) {
        error_report(ERROR_SOURCE_CAM, DRV_ERR_PARAM);
        return;
    }

    cam_inst.huart = huart;
    cam_inst.rx_read_pos = 0;
    cam_inst.rx_write_pos = 0;
    cam_inst.rx_state = CAM_STATE_WAIT_HEADER;
    cam_inst.frame_index = 0;
    cam_frame_ready = 0;

    memset(cam_inst.dma_rx_buffer, 0, CAM_DMA_RX_BUF_SIZE);
    status = HAL_UARTEx_ReceiveToIdle_DMA(cam_inst.huart, cam_inst.dma_rx_buffer,
        CAM_DMA_RX_BUF_SIZE);
    if (status == HAL_BUSY) {
        error_report(ERROR_SOURCE_CAM, DRV_ERR_BUSY);
    } else if (status != HAL_OK) {
        error_report(ERROR_SOURCE_CAM, DRV_ERR_IO);
    }
}

/**
 * @brief  DMA 接收完成回调（ISR 中调用），将数据推入 RX FIFO
 * @param  huart  触发中断的串口句柄
 * @param  size   实际接收数据长度
 * @retval 无
 */
void cam_rx_callback(UART_HandleTypeDef *huart, uint16_t size)
{
    HAL_StatusTypeDef status;
    uint16_t next;
    uint16_t i;

    if (!huart || !cam_inst.huart) {
        error_report(ERROR_SOURCE_CAM, DRV_ERR_PARAM);
        return;
    }

    if (huart->Instance != cam_inst.huart->Instance) {
        error_report(ERROR_SOURCE_CAM, DRV_ERR_PARAM);
        return;
    }

    if (size > 0) {
        for (i = 0; i < size; i++) {
            next = (cam_inst.rx_write_pos + 1) % CAM_RX_FIFO_SIZE;
            if (next != cam_inst.rx_read_pos) {
                cam_inst.rx_fifo[cam_inst.rx_write_pos] = cam_inst.dma_rx_buffer[i];
                cam_inst.rx_write_pos = next;
            }
        }
    }

    memset(cam_inst.dma_rx_buffer, 0, CAM_DMA_RX_BUF_SIZE);
    status = HAL_UARTEx_ReceiveToIdle_DMA(cam_inst.huart, cam_inst.dma_rx_buffer,
        CAM_DMA_RX_BUF_SIZE);
    if (status == HAL_BUSY) {
        error_report(ERROR_SOURCE_CAM, DRV_ERR_BUSY);
    } else if (status != HAL_OK) {
        error_report(ERROR_SOURCE_CAM, DRV_ERR_IO);
    }
}

/**
 * @brief  摄像头周期性任务（主循环中调用）
 * @note   帧解析状态机：帧头 0xFF → 数据 → 帧尾 0xFE
 * @param  无
 * @retval 无
 */
void cam_task(void)
{
    uint8_t byte;

    while (cam_inst.rx_read_pos != cam_inst.rx_write_pos) {
        byte = cam_inst.rx_fifo[cam_inst.rx_read_pos];
        cam_inst.rx_read_pos = (cam_inst.rx_read_pos + 1) % CAM_RX_FIFO_SIZE;

        switch (cam_inst.rx_state) {
            case CAM_STATE_WAIT_HEADER:
                if (byte == CAM_FRAME_HEADER) {
                    cam_inst.frame_index = 0;
                    cam_inst.rx_state = CAM_STATE_RECEIVING_DATA;
                }
                break;

            case CAM_STATE_RECEIVING_DATA:
                if (byte == CAM_FRAME_TAIL) {
                    if (cam_inst.frame_index < CAM_MAX_FRAME_LEN) {
                        cam_inst.frame_buffer[cam_inst.frame_index] = '\0';
                    }

                    /* 数据解析：[is_junction, direction, green, stop, deviation] */
                    if (cam_inst.frame_index == CAM_FRAME_DATA_LEN) {
                        cam_data.is_junction = cam_inst.frame_buffer[0];
                        cam_data.direction   = cam_inst.frame_buffer[1];
                        cam_data.green       = cam_inst.frame_buffer[2];
                        cam_data.stop        = cam_inst.frame_buffer[3];
                        cam_data.deviation   = (int8_t)cam_inst.frame_buffer[4];
                    }

                    cam_frame_ready = 1;
                    cam_inst.rx_state = CAM_STATE_WAIT_HEADER;
                } else if (byte == CAM_FRAME_HEADER) {
                    cam_inst.frame_index = 0;
                } else if (byte == CAM_ESC_BYTE) {
                    cam_inst.rx_state = CAM_STATE_ESCAPE;
                } else {
                    if (cam_inst.frame_index < CAM_MAX_FRAME_LEN) {
                        cam_inst.frame_buffer[cam_inst.frame_index++] = byte;
                    } else {
                        cam_inst.rx_state = CAM_STATE_WAIT_HEADER;
                    }
                }
                break;

            case CAM_STATE_ESCAPE:
                if (byte == CAM_ESC_TAIL) {
                    byte = CAM_FRAME_TAIL;
                } else if (byte == CAM_ESC_HEADER) {
                    byte = CAM_FRAME_HEADER;
                } else if (byte == CAM_ESC_ESC) {
                    byte = CAM_ESC_BYTE;
                }
                if (cam_inst.frame_index < CAM_MAX_FRAME_LEN) {
                    cam_inst.frame_buffer[cam_inst.frame_index++] = byte;
                } else {
                    cam_inst.rx_state = CAM_STATE_WAIT_HEADER;
                }
                cam_inst.rx_state = CAM_STATE_RECEIVING_DATA;
                break;

            default:
                cam_inst.rx_state = CAM_STATE_WAIT_HEADER;
                break;
        }
    }
}

/**
 * @brief  获取摄像头解析数据
 * @param  无
 * @retval 摄像头数据结构体
 */
cam_data_t cam_get_data(void)
{
    return cam_data;
}
