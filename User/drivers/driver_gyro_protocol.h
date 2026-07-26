/**
 * @file driver_gyro_protocol.h
 * @brief WIT 0x55/0x53 angle frame parser.
 */
#ifndef USER_DRIVERS_DRIVER_GYRO_PROTOCOL_H
#define USER_DRIVERS_DRIVER_GYRO_PROTOCOL_H

#include <stdint.h>

#include "status.h"

typedef struct {
    float roll;
    float pitch;
    float yaw;
    uint8_t valid;
    uint32_t sequence;
} driver_gyro_attitude_t;

typedef struct {
    uint8_t frame[11];
    uint8_t index;
    driver_gyro_attitude_t attitude;
} driver_gyro_protocol_t;

void driver_gyro_protocol_init(driver_gyro_protocol_t *protocol);
void driver_gyro_protocol_push(driver_gyro_protocol_t *protocol,
                              const uint8_t *data, uint16_t length);
status_code_t driver_gyro_protocol_snapshot(const driver_gyro_protocol_t *protocol,
                                            driver_gyro_attitude_t *attitude);

#endif
