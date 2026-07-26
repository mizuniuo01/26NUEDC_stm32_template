/**
 * @file driver_gpio_output.c
 * @brief Fixed GPIO output bank driver implementation.
 */
#include "driver_gpio_output.h"

#include <string.h>

status_code_t driver_gpio_output_init(driver_gpio_output_bank_t *bank,
                                      const driver_gpio_output_pin_t *pins, uint8_t count)
{
    if ((bank == NULL) || (pins == NULL) || (count == 0U) ||
        (count > DRIVER_GPIO_OUTPUT_MAX)) {
        return STATUS_INVALID_ARGUMENT;
    }
    (void)memcpy(bank->pins, pins, (size_t)count * sizeof(bank->pins[0]));
    bank->count = count;
    bank->initialized = 1U;
    return driver_gpio_output_set_mask(bank, 0U);
}

status_code_t driver_gpio_output_set(driver_gpio_output_bank_t *bank, uint8_t index, uint8_t active)
{
    GPIO_PinState inactive_level;

    if ((bank == NULL) || (bank->initialized == 0U)) {
        return STATUS_NOT_INITIALIZED;
    }
    if (index >= bank->count) {
        return STATUS_OUT_OF_RANGE;
    }
    inactive_level = bank->pins[index].active_level == GPIO_PIN_SET ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(bank->pins[index].port, bank->pins[index].pin,
                      active != 0U ? bank->pins[index].active_level : inactive_level);
    return STATUS_OK;
}

status_code_t driver_gpio_output_set_mask(driver_gpio_output_bank_t *bank, uint8_t mask)
{
    uint8_t i;
    status_code_t status;

    if ((bank == NULL) || (bank->initialized == 0U)) {
        return STATUS_NOT_INITIALIZED;
    }
    for (i = 0U; i < bank->count; i++) {
        status = driver_gpio_output_set(bank, i, (uint8_t)((mask >> i) & 1U));
        if (status != STATUS_OK) {
            return status;
        }
    }
    return STATUS_OK;
}
