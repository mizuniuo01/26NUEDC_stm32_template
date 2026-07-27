/**
 * @file bsp_board_stm32_callbacks.c
 * @brief 将 STM32 HAL 回调转发到 BSP 板级中断入口。
 * @note 本平台适配文件位于 User 目录，不会被 CubeMX 重新生成覆盖。
 */
#include "bsp_board.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "stm32f407xx.h"

/**
 * @brief  将 TIM6 周期事件转发到板级时间基准
 * @param  timer STM32 HAL 提供的定时器句柄
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *timer)
{
    if (timer && (timer->Instance == TIM6)) {
        bsp_board_timer_elapsed_isr();
    }
}

/**
 * @brief  将定时器输入捕获事件转发到板级适配器
 * @param  timer STM32 HAL 提供的定时器句柄
 */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *timer)
{
    if (timer) {
        bsp_board_timer_capture_isr(timer, TIM_CHANNEL_4);
    }
}

/**
 * @brief  将串口接收到空闲事件转发到板级适配器
 * @param  uart STM32 HAL 提供的串口句柄
 * @param  size 当前 DMA 缓冲区收到的字节数
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *uart, uint16_t size)
{
    bsp_board_uart_rx_event_isr(uart, size);
}

/**
 * @brief  将串口发送完成事件转发到板级适配器
 * @param  uart STM32 HAL 提供的串口句柄
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
    bsp_board_uart_tx_complete_isr(uart);
}

/**
 * @brief  将串口错误事件转发到板级适配器
 * @param  uart STM32 HAL 提供的串口句柄
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    bsp_board_uart_error_isr(uart);
}

/**
 * @brief  将 I2C 存储器读取完成事件转发到板级适配器
 * @param  i2c STM32 HAL 提供的 I2C 句柄
 */
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *i2c)
{
    bsp_board_i2c_rx_complete_isr(i2c);
}

/**
 * @brief  将 I2C 主机发送完成事件转发到板级适配器
 * @param  i2c STM32 HAL 提供的 I2C 句柄
 */
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *i2c)
{
    bsp_board_i2c_tx_complete_isr(i2c);
}

/**
 * @brief  将 I2C 错误事件转发到板级适配器
 * @param  i2c STM32 HAL 提供的 I2C 句柄
 */
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *i2c)
{
    bsp_board_i2c_error_isr(i2c);
}
