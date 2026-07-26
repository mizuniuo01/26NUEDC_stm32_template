/**
 * @file driver_keys.c
 * @brief Debounced GPIO key driver implementation.
 */
#include "driver_keys.h"

#include <string.h>

static uint8_t read_state(const driver_keys_t *keys)
{
    uint8_t state = 0U;
    uint8_t i;

    for (i = 0U; i < DRIVER_KEYS_COUNT; i++) {
        if (HAL_GPIO_ReadPin(keys->pins[i].port, keys->pins[i].pin) ==
            keys->pins[i].active_level) {
            state |= (uint8_t)(1U << i);
        }
    }
    return state;
}

status_code_t driver_keys_init(driver_keys_t *keys, const driver_key_pin_t *pins,
                               uint32_t debounce_ms)
{
    if ((keys == NULL) || (pins == NULL)) {
        return STATUS_INVALID_ARGUMENT;
    }
    (void)memcpy(keys->pins, pins, sizeof(keys->pins));
    keys->stable_state = read_state(keys);
    keys->candidate_state = keys->stable_state;
    keys->pressed_events = 0U;
    keys->changed_at_ms = 0U;
    keys->debounce_ms = debounce_ms;
    keys->initialized = 1U;
    return STATUS_OK;
}

status_code_t driver_keys_process(driver_keys_t *keys, uint32_t now_ms)
{
    uint8_t current;

    if ((keys == NULL) || (keys->initialized == 0U)) {
        return STATUS_NOT_INITIALIZED;
    }
    current = read_state(keys);
    if (current != keys->stable_state) {
        if (current != keys->candidate_state) {
            keys->candidate_state = current;
            keys->changed_at_ms = now_ms;
        } else if ((uint32_t)(now_ms - keys->changed_at_ms) >= keys->debounce_ms) {
            keys->pressed_events |= (uint8_t)(current & (uint8_t)~keys->stable_state);
            keys->stable_state = current;
        }
    } else {
        keys->candidate_state = current;
    }
    return STATUS_OK;
}

uint8_t driver_keys_state(const driver_keys_t *keys)
{
    return (keys == NULL) ? 0U : keys->stable_state;
}

uint8_t driver_keys_take_pressed(driver_keys_t *keys)
{
    uint8_t events;

    if (keys == NULL) {
        return 0U;
    }
    events = keys->pressed_events;
    keys->pressed_events = 0U;
    return events;
}
