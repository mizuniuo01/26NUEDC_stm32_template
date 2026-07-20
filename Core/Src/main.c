/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "i2c.h"
#include "iwdg.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "system.h"
#include "error_handler.h"
#include "blueteeth.h"
#include "bldc.h"
#include "buzzer.h"
#include "cam.h"
#include "encoder.h"
#include "gyroscope.h"
#include "led.h"
#include "motor.h"
#include "pid.h"
#include "pwm.h"
#include "pattern.h"
#include "sensor.h"
#include "display.h"
#include "bt_command.h"
#include "motion_control.h"
#include "motion_manager.h"
#include "perception.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* TIM6 1ms 中断分频阈值，统一管理各模块 tick 周期 */
typedef enum {
    TICK_GYRO_CNT = 1,  /* 陀螺仪 1ms */
    TICK_SENSOR_CNT = 3,  /* 传感器 3ms */
    TICK_DISPLAY_CNT = 50,  /* 显示刷新 50ms */
    TICK_BLDC_FEEDBACK_CNT = 10, /* 无刷电机反馈轮询 10ms */
    TICK_ENCODER_CNT = 10, /* 编码器 10ms */
    TICK_MOTION_CONTROL_CNT = 10, /* 底层闭环 10ms */
    TICK_MOTION_MANAGER_CNT = 10, /* 运动管理 10ms */
    TICK_CONTROL_MANAGER_CNT = 10, /* 总控调度 10ms */
    TICK_SYSTEM_LED_CNT = 50, /* 系统 LED 50ms */
} main_tick_cfg_t;

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* TIM6 ISR 置位，主循环消费的无刷电机反馈请求标志。 */
static volatile uint8_t bldc_feedback_tick_flag;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

static void bldc_feedback_request_task(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief  轮询请求 X/Y 无刷电机的各项反馈数据。
 * @return 无。
 * @note   每次 TIM6 节拍只加入一个请求；两个电机和五种反馈约 100ms 轮询一轮。
 */
static void bldc_feedback_request_task(void)
{
    static uint8_t motor_index;
    static bldc_feedback_type_t feedback_type = BLDC_FEEDBACK_SPEED;
    bldc_motor_t *motor;

    if (!bldc_feedback_tick_flag) {
        return;
    }
    bldc_feedback_tick_flag = 0U;

    motor = (motor_index == 0U) ? system_bldc_x() : system_bldc_y();
    if (bldc_request_feedback(motor, feedback_type) != BLDC_STATUS_OK) {
        return;
    }

    motor_index++;
    if (motor_index >= 2U) {
        motor_index = 0U;
        feedback_type = (bldc_feedback_type_t)((uint8_t)feedback_type + 1U);
        if (feedback_type >= BLDC_FEEDBACK_COUNT) {
            feedback_type = BLDC_FEEDBACK_SPEED;
        }
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
    SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
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
  /* USER CODE BEGIN 2 */
    HAL_TIM_Base_Start_IT(&htim6);
    system_init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1) {
        system_state();
        error_handler_task();
        blueteeth_task();
        bldc_task(system_bldc_bus());
        bldc_feedback_request_task();
        display_task();
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.LSIState = RCC_LSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 6;
    RCC_OscInitStruct.PLL.PLLN = 168;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

  /** Initializes the CPU, AHB and APB buses clocks
  */
    RCC_ClkInitStruct.ClockType =
        RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
        Error_Handler();
    }
}

/* USER CODE BEGIN 4 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6) {
        static uint8_t system_led_cnt = 0;
        static uint8_t gyro_tick_cnt = 0;
        static uint8_t sensor_tick_cnt = 0;
        static uint8_t encoder_tick_cnt = 0;
        static uint8_t display_tick_cnt = 0;
        static uint8_t bldc_feedback_tick_cnt = 0;
        static uint8_t motion_control_tick_cnt = 0;
        static uint8_t motion_manager_tick_cnt = 0;

    /* 系统运行状态指示灯标志位 */
        system_led_cnt++;
        if (system_led_cnt >= TICK_SYSTEM_LED_CNT) {
            system_led_cnt = 0;
            set_system_led_flag(1);
        }

    /* 陀螺仪数据服务标志位 */
        gyro_tick_cnt++;
        if (gyro_tick_cnt >= TICK_GYRO_CNT) {
            gyro_tick_cnt = 0;
            gyro_tick_flag = 1;
        }

    /* 传感器数据服务标志位 */
        sensor_tick_cnt++;
        if (sensor_tick_cnt >= TICK_SENSOR_CNT) {
            sensor_tick_cnt = 0;
            sensor_tick_flag = 1;
        }

    /* 编码器周期扫描 */
        encoder_tick_cnt++;
        if (encoder_tick_cnt >= TICK_ENCODER_CNT) {
            encoder_tick_cnt = 0;
            encoder_scan_left(&htim2);
            encoder_scan_right(&htim1);
        }

    /* 打印刷新标志位 */
        display_tick_cnt++;
        if (display_tick_cnt >= TICK_DISPLAY_CNT) {
            display_tick_cnt = 0;
            display_refresh_flag = 1;
        }

    /* 无刷电机反馈轮询标志位 */
        bldc_feedback_tick_cnt++;
        if (bldc_feedback_tick_cnt >= TICK_BLDC_FEEDBACK_CNT) {
            bldc_feedback_tick_cnt = 0;
            bldc_feedback_tick_flag = 1;
        }

    /* 底层闭环服务标志位 */
        motion_control_tick_cnt++;
        if (motion_control_tick_cnt >= TICK_MOTION_CONTROL_CNT) {
            motion_control_tick_cnt = 0;
            motion_control_tick_flag = 1;
        }

    /* 运动管理服务标志位 */
        motion_manager_tick_cnt++;
        if (motion_manager_tick_cnt >= TICK_MOTION_MANAGER_CNT) {
            motion_manager_tick_cnt = 0;
            motion_manager_tick_flag = 1;
        }
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART1) {
        blueteeth_rx_callback(huart, Size);
    }

    if (huart->Instance == USART3) {
        cam_rx_callback(huart, Size);
    }

    if (huart->Instance == USART6) {
        gyro_rx_callback(huart, Size);
    }

    if (huart->Instance == USART2) {
        bldc_rx_callback(system_bldc_bus(), huart, Size);
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        blueteeth_tx_callback(huart);
    }

    if (huart->Instance == USART2) {
        bldc_tx_callback(system_bldc_bus(), huart);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART6) {
        gyro_error_callback(huart);
    }

    if (huart->Instance == USART2) {
        bldc_error_callback(system_bldc_bus(), huart);
    }
}

void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C2) {
        sensor_rx_callback(hi2c);
    }
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C2) {
        sensor_error_callback(hi2c);
    }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1) {
    }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
