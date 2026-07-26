/**
 * @file driver_camera_protocol.h
 * @brief MaixCAM escaped binary protocol codec.
 */
#ifndef USER_DRIVERS_DRIVER_CAMERA_PROTOCOL_H
#define USER_DRIVERS_DRIVER_CAMERA_PROTOCOL_H

#include <stdint.h>

#include "status.h"

#define DRIVER_CAMERA_LOGICAL_MAX 132U

typedef struct {
    int16_t error_x;
    int16_t error_y;
    uint8_t has_target;
    uint8_t switch_ack;
    uint8_t switch_ack_id;
    uint8_t valid;
    uint32_t sequence;
} driver_camera_target_t;

typedef struct {
    uint8_t frame[DRIVER_CAMERA_LOGICAL_MAX];
    uint16_t length;
    uint8_t state;
    driver_camera_target_t target;
} driver_camera_protocol_t;

void driver_camera_protocol_init(driver_camera_protocol_t *protocol);
void driver_camera_protocol_push(driver_camera_protocol_t *protocol,
                                 const uint8_t *data, uint16_t length);
status_code_t driver_camera_protocol_snapshot(const driver_camera_protocol_t *protocol,
                                              driver_camera_target_t *target);
status_code_t driver_camera_protocol_encode_switch(uint8_t enabled, uint8_t request_id,
                                                   uint8_t *frame, uint16_t capacity,
                                                   uint16_t *length);

#endif
