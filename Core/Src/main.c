/* USER CODE BEGIN Header */
/**
  * @file main.c
  * @brief STM32 entry point for the bottom-layer refactoring smoke target.
  */
/* USER CODE END Header */
#include "main.h"
#include "dma.h"
#include "gpio.h"
#include "i2c.h"
#include "iwdg.h"
#include "tim.h"
#include "usart.h"

#include "bsp_board.h"
#include "refactor_smoke.h"

void SystemClock_Config(void);

int main(void)
{
    status_code_t status;

    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_TIM1_Init();
    MX_TIM2_Init();
    MX_TIM3_Init();
    MX_TIM6_Init();
    MX_USART1_UART_Init();
    MX_USART3_UART_Init();
    MX_USART6_UART_Init();
    MX_I2C2_Init();
    MX_I2C3_Init();
    MX_TIM4_Init();
    MX_UART4_Init();
    MX_USART2_UART_Init();
    MX_IWDG_Init();

    if (HAL_TIM_Base_Start_IT(&htim6) != HAL_OK) {
        Error_Handler();
    }
    status = bsp_board_init();
    if (status != STATUS_OK) {
        Error_Handler();
    }
    refactor_smoke_init();

    while (1) {
        bsp_board_process();
        refactor_smoke_process();
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clock = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_HSE;
    oscillator.HSEState = RCC_HSE_ON;
    oscillator.LSIState = RCC_LSI_ON;
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscillator.PLL.PLLM = 6;
    oscillator.PLL.PLLN = 168;
    oscillator.PLL.PLLP = RCC_PLLP_DIV2;
    oscillator.PLL.PLLQ = 4;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) {
        Error_Handler();
    }
    clock.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                      RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clock.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock.APB1CLKDivider = RCC_HCLK_DIV4;
    clock.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_5) != HAL_OK) {
        Error_Handler();
    }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *timer)
{
    if ((timer != NULL) && (timer->Instance == TIM6)) {
        bsp_board_timer_elapsed_isr();
    }
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *timer)
{
    if (timer != NULL) {
        bsp_board_timer_capture_isr(timer, TIM_CHANNEL_4);
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *uart, uint16_t size)
{
    bsp_board_uart_rx_event_isr(uart, size);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
    bsp_board_uart_tx_complete_isr(uart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    bsp_board_uart_error_isr(uart);
}

void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *i2c)
{
    bsp_board_i2c_rx_complete_isr(i2c);
}

void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *i2c)
{
    bsp_board_i2c_tx_complete_isr(i2c);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *i2c)
{
    bsp_board_i2c_error_isr(i2c);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
}
#endif
