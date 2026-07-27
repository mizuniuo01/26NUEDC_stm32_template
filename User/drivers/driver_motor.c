/**
 * @file driver_motor.c
 * @brief 使用 DRV8874 PH/EN 接口控制双路底盘直流电机。
 */
#include "driver_motor.h"
#include <stdlib.h>

/**
 * @brief  将有符号电机目标换算为有效 PWM 比较值
 * @param  value 有符号电机目标，符号表示方向
 * @param  maximum PWM 比较值上限
 * @param  minimum 克服静摩擦所需的最小有效比较值
 * @return 经过上限和死区处理的非负比较值
 */
static uint16_t clamp_compare(int16_t value, uint16_t maximum, uint16_t minimum)
{
    uint16_t compare = (uint16_t)abs(value);

    if (compare > maximum) {
        compare = maximum;
    }
    if (compare < minimum) {
        compare = 0U;
    }
    return compare;
}

/**
 * @brief  更新一路电机的方向和 PWM 比较值
 * @param  motor 已初始化的双路电机驱动实例
 * @param  is_right 右电机选择标志，零选择左电机
 * @param  value 有符号电机目标，符号表示方向
 */
static void write_motor(driver_motor_t *motor, bool is_right, int16_t value)
{
    GPIO_TypeDef *direction_port =
        is_right ? motor->config.right_direction_port : motor->config.left_direction_port;
    uint16_t direction_pin =
        is_right ? motor->config.right_direction_pin : motor->config.left_direction_pin;
    uint32_t channel = is_right ? motor->config.right_channel : motor->config.left_channel;
    GPIO_PinState forward_level;
    uint16_t compare =
        clamp_compare(value, motor->config.max_compare, motor->config.minimum_effective_compare);

    /* 左右电机安装方向相反，因此正向行驶使用相反的 PH 电平。 */
    forward_level = is_right ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(direction_port, direction_pin,
        value >= 0 ? forward_level
                   : (forward_level == GPIO_PIN_SET ? GPIO_PIN_RESET : GPIO_PIN_SET));
    __HAL_TIM_SET_COMPARE(motor->config.timer, channel, compare);
}

/**
 * @brief  初始化双路电机驱动并保持功率输出禁用
 * @param  motor 电机驱动实例
 * @param  config 定时器、GPIO 和 PWM 限幅配置
 * @retval STATUS_OK 两路 PWM 已启动且电机保持休眠
 * @retval STATUS_INVALID_ARGUMENT 实例、配置或定时器句柄为空
 * @retval STATUS_IO_ERROR 任一路 PWM 无法启动
 */
status_code_t driver_motor_init(driver_motor_t *motor, const driver_motor_config_t *config)
{
    if (!motor || !config || !config->timer) {
        return STATUS_INVALID_ARGUMENT;
    }
    motor->config = *config;
    motor->is_initialized = true;
    motor->is_enabled = false;
    HAL_GPIO_WritePin(config->left_sleep_port, config->left_sleep_pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(config->right_sleep_port, config->right_sleep_pin, GPIO_PIN_RESET);
    __HAL_TIM_SET_COMPARE(config->timer, config->left_channel, 0U);
    __HAL_TIM_SET_COMPARE(config->timer, config->right_channel, 0U);
    if (HAL_TIM_PWM_Start(config->timer, config->left_channel) != HAL_OK) {
        return STATUS_IO_ERROR;
    }
    if (HAL_TIM_PWM_Start(config->timer, config->right_channel) != HAL_OK) {
        return STATUS_IO_ERROR;
    }
    return STATUS_OK;
}

/**
 * @brief  唤醒两路电机功率输出
 * @param  motor 电机驱动实例
 * @retval STATUS_OK 电机输出已使能
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 */
status_code_t driver_motor_enable(driver_motor_t *motor)
{
    if (!motor || !motor->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    HAL_GPIO_WritePin(motor->config.left_sleep_port, motor->config.left_sleep_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(motor->config.right_sleep_port, motor->config.right_sleep_pin, GPIO_PIN_SET);
    motor->is_enabled = true;
    return STATUS_OK;
}

/**
 * @brief  将两路 PWM 清零并关闭电机功率输出
 * @param  motor 电机驱动实例
 * @retval STATUS_OK 电机输出已禁用
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 */
status_code_t driver_motor_disable(driver_motor_t *motor)
{
    if (!motor || !motor->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    __HAL_TIM_SET_COMPARE(motor->config.timer, motor->config.left_channel, 0U);
    __HAL_TIM_SET_COMPARE(motor->config.timer, motor->config.right_channel, 0U);
    HAL_GPIO_WritePin(motor->config.left_sleep_port, motor->config.left_sleep_pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(motor->config.right_sleep_port, motor->config.right_sleep_pin,
        GPIO_PIN_RESET);
    motor->is_enabled = false;
    return STATUS_OK;
}

/**
 * @brief  设置左右电机的有符号 PWM 目标
 * @param  motor 电机驱动实例
 * @param  left 左电机目标比较值，符号表示方向
 * @param  right 右电机目标比较值，符号表示方向
 * @retval STATUS_OK 两路电机输出已更新
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 * @retval STATUS_STATE_ERROR 电机功率输出尚未使能
 */
status_code_t driver_motor_set(driver_motor_t *motor, int16_t left, int16_t right)
{
    if (!motor || !motor->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (!motor->is_enabled) {
        return STATUS_STATE_ERROR;
    }
    write_motor(motor, false, left);
    write_motor(motor, true, right);
    return STATUS_OK;
}
