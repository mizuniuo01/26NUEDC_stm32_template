/**
 * @file driver_gpio_output.c
 * @brief 管理一组具有独立有效电平语义的固定 GPIO 输出。
 */
#include "driver_gpio_output.h"
#include <string.h>

/**
 * @brief  初始化固定 GPIO 输出组并关闭全部输出
 * @param  bank GPIO 输出组实例
 * @param  pins 输出引脚配置数组
 * @param  count pins 中的有效配置数量
 * @retval STATUS_OK 输出组已初始化且全部关闭
 * @retval STATUS_INVALID_ARGUMENT 参数为空或数量超出容量
 */
status_code_t driver_gpio_output_init(driver_gpio_output_bank_t *bank,
    const driver_gpio_output_pin_t *pins, uint8_t count)
{
    if (!bank || !pins || (count == 0U) || (count > DRIVER_GPIO_OUTPUT_CAPACITY)) {
        return STATUS_INVALID_ARGUMENT;
    }
    (void)memcpy(bank->pins, pins, (size_t)count * sizeof(bank->pins[0]));
    bank->count = count;
    bank->is_initialized = true;
    return driver_gpio_output_set_mask(bank, 0U);
}

/**
 * @brief  设置输出组中的一个语义输出
 * @param  bank GPIO 输出组实例
 * @param  index 输出索引
 * @param  is_active 语义输出有效标志
 * @retval STATUS_OK GPIO 已更新
 * @retval STATUS_NOT_INITIALIZED 输出组为空或尚未初始化
 * @retval STATUS_OUT_OF_RANGE index 超出已配置输出数量
 */
status_code_t driver_gpio_output_set(driver_gpio_output_bank_t *bank, uint8_t index, bool is_active)
{
    GPIO_PinState inactive_level;

    if (!bank || !bank->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (index >= bank->count) {
        return STATUS_OUT_OF_RANGE;
    }
    inactive_level = bank->pins[index].active_level == GPIO_PIN_SET ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(bank->pins[index].port, bank->pins[index].pin,
        is_active ? bank->pins[index].active_level : inactive_level);
    return STATUS_OK;
}

/**
 * @brief  按位掩码设置输出组中的全部语义输出
 * @param  bank GPIO 输出组实例
 * @param  mask 每一位非零表示对应输出有效
 * @retval STATUS_OK 全部 GPIO 已更新
 * @retval STATUS_NOT_INITIALIZED 输出组为空或尚未初始化
 */
status_code_t driver_gpio_output_set_mask(driver_gpio_output_bank_t *bank, uint8_t mask)
{
    uint8_t i;
    status_code_t status;

    if (!bank || !bank->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    for (i = 0U; i < bank->count; i++) {
        status = driver_gpio_output_set(bank, i, ((mask >> i) & 1U) != 0U);
        if (status != STATUS_OK) {
            return status;
        }
    }
    return STATUS_OK;
}
