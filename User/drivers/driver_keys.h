/**
 * @file driver_keys.h
 * @brief Debounced GPIO key driver.
 */
#ifndef USER_DRIVERS_DRIVER_KEYS_H
#define USER_DRIVERS_DRIVER_KEYS_H

#include "main.h"
#include "status.h"

#define DRIVER_KEYS_COUNT 5U

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    GPIO_PinState active_level;
} driver_key_pin_t;

typedef struct {
    driver_key_pin_t pins[DRIVER_KEYS_COUNT];
    uint8_t stable_state;
    uint8_t candidate_state;
    uint8_t pressed_events;
    uint8_t initialized;
    uint32_t changed_at_ms;
    uint32_t debounce_ms;
} driver_keys_t;

status_code_t driver_keys_init(driver_keys_t *keys, const driver_key_pin_t *pins,
                               uint32_t debounce_ms);
status_code_t driver_keys_process(driver_keys_t *keys, uint32_t now_ms);
uint8_t driver_keys_state(const driver_keys_t *keys);
uint8_t driver_keys_take_pressed(driver_keys_t *keys);

#endif
