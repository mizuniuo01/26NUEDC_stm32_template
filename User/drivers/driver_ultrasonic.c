/**
 * @file driver_ultrasonic.c
 * @brief 使用 GPIO 触发和定时器输入捕获执行非阻塞 HC-SR04 测距。
 */
#include "driver_ultrasonic.h"

/**
 * @brief  初始化 HC-SR04 驱动并启动回波输入捕获
 * @param  ultrasonic 超声波驱动实例
 * @param  config 定时器、输入捕获通道、触发 GPIO 和周期配置
 * @retval STATUS_OK 输入捕获中断已启动
 * @retval STATUS_INVALID_ARGUMENT 实例、配置、定时器或触发端口为空
 * @retval STATUS_IO_ERROR HAL 无法启动输入捕获中断
 */
status_code_t driver_ultrasonic_init(driver_ultrasonic_t *ultrasonic,
    const driver_ultrasonic_config_t *config)
{
    if (!ultrasonic || !config || !config->timer || !config->trigger_port) {
        return STATUS_INVALID_ARGUMENT;
    }
    ultrasonic->config = *config;
    ultrasonic->rising_capture = 0U;
    ultrasonic->distance_mm = 0U;
    ultrasonic->is_waiting_for_fall = false;
    ultrasonic->is_valid = false;
    ultrasonic->last_trigger_ms = 0U;
    ultrasonic->is_initialized = true;
    HAL_GPIO_WritePin(config->trigger_port, config->trigger_pin, GPIO_PIN_RESET);
    return HAL_TIM_IC_Start_IT(config->timer, config->channel) == HAL_OK ? STATUS_OK
                                                                         : STATUS_IO_ERROR;
}

/**
 * @brief  按配置周期更新 HC-SR04 触发引脚
 * @param  ultrasonic 超声波驱动实例
 * @param  now_ms 当前单调时间，单位：毫秒
 * @retval STATUS_OK 触发引脚已更新
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 */
status_code_t driver_ultrasonic_process(driver_ultrasonic_t *ultrasonic, uint32_t now_ms)
{
    if (!ultrasonic || !ultrasonic->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if ((uint32_t)(now_ms - ultrasonic->last_trigger_ms) >= ultrasonic->config.trigger_period_ms) {
        ultrasonic->last_trigger_ms = now_ms;
        HAL_GPIO_WritePin(ultrasonic->config.trigger_port, ultrasonic->config.trigger_pin,
            GPIO_PIN_SET);
        /* 触发引脚会在下一次周期处理时恢复低电平。 */
    } else {
        HAL_GPIO_WritePin(ultrasonic->config.trigger_port, ultrasonic->config.trigger_pin,
            GPIO_PIN_RESET);
    }
    return STATUS_OK;
}

/**
 * @brief  处理回波边沿并将脉宽换算为距离
 * @param  ultrasonic 超声波驱动实例
 * @param  timer 发生捕获事件的 STM32 HAL 定时器句柄
 * @param  channel 发生捕获事件的 HAL 定时器通道
 */
void driver_ultrasonic_capture_isr(driver_ultrasonic_t *ultrasonic, TIM_HandleTypeDef *timer,
    uint32_t channel)
{
    uint32_t capture;
    uint32_t width;

    if (!ultrasonic || !timer || (timer != ultrasonic->config.timer) ||
        (channel != ultrasonic->config.channel) || !ultrasonic->is_initialized) {
        return;
    }
    capture = HAL_TIM_ReadCapturedValue(timer, channel);
    if (!ultrasonic->is_waiting_for_fall) {
        ultrasonic->rising_capture = capture;
        ultrasonic->is_waiting_for_fall = true;
        __HAL_TIM_SET_CAPTUREPOLARITY(timer, channel, TIM_INPUTCHANNELPOLARITY_FALLING);
    } else {
        width = (capture >= ultrasonic->rising_capture)
                    ? (capture - ultrasonic->rising_capture)
                    : (UINT32_C(0x10000) - ultrasonic->rising_capture + capture);
        ultrasonic->distance_mm = (uint16_t)((width * UINT32_C(10)) / UINT32_C(58));
        ultrasonic->is_valid = true;
        ultrasonic->is_waiting_for_fall = false;
        __HAL_TIM_SET_CAPTUREPOLARITY(timer, channel, TIM_INPUTCHANNELPOLARITY_RISING);
    }
}

/**
 * @brief  获取最近一次超声波测距结果
 * @param  ultrasonic 超声波驱动实例
 * @param  distance_mm 可选的距离输出地址，单位：毫米
 * @param  is_valid 可选的距离有效标志输出地址
 * @retval STATUS_OK 所有非空输出地址均已写入
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 */
status_code_t driver_ultrasonic_read(const driver_ultrasonic_t *ultrasonic, uint16_t *distance_mm,
    bool *is_valid)
{
    if (!ultrasonic || !ultrasonic->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (distance_mm) {
        *distance_mm = ultrasonic->distance_mm;
    }
    if (is_valid) {
        *is_valid = ultrasonic->is_valid;
    }
    return STATUS_OK;
}
