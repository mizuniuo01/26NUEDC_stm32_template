/**
 * @file driver_camera_protocol.c
 * @brief MaixCAM escaped binary protocol codec implementation.
 */
#include "driver_camera_protocol.h"

#include <string.h>

enum {
    CAMERA_WAIT_HEADER = 0,
    CAMERA_RECEIVING = 1,
    CAMERA_ESCAPE = 2,
    CAMERA_HEADER = 0xFF,
    CAMERA_TAIL = 0xFE,
    CAMERA_ESCAPE_BYTE = 0x7D,
    CAMERA_ESCAPE_HEADER = 0x5F,
    CAMERA_ESCAPE_TAIL = 0x5E,
    CAMERA_ESCAPE_ESCAPE = 0x5D,
    CAMERA_VERSION = 0x01,
    CAMERA_TARGET_REPORT = 0x01,
    CAMERA_SWITCH_COMMAND = 0x10
};

static uint8_t crc8(const uint8_t *data, uint16_t length)
{
    uint8_t crc = 0U;
    uint16_t i;
    uint8_t bit;

    for (i = 0U; i < length; i++) {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++) {
            crc = (crc & 0x80U) != 0U ? (uint8_t)((crc << 1U) ^ 0x07U) :
                                       (uint8_t)(crc << 1U);
        }
    }
    return crc;
}

static void process_frame(driver_camera_protocol_t *protocol)
{
    uint8_t payload_length;
    const uint8_t *payload;

    if (protocol->length < 4U) {
        return;
    }
    payload_length = protocol->frame[2];
    if ((protocol->frame[0] != CAMERA_VERSION) ||
        (protocol->length != (uint16_t)(payload_length + 4U)) ||
        (crc8(protocol->frame, protocol->length - 1U) !=
         protocol->frame[protocol->length - 1U])) {
        return;
    }
    if ((protocol->frame[1] != CAMERA_TARGET_REPORT) || (payload_length != 6U)) {
        return;
    }
    payload = &protocol->frame[3];
    if ((payload[0] & (uint8_t)~0x03U) != 0U) {
        return;
    }
    protocol->target.has_target = (uint8_t)(payload[0] & 0x01U);
    protocol->target.switch_ack = (uint8_t)((payload[0] >> 1U) & 0x01U);
    protocol->target.switch_ack_id = protocol->target.switch_ack != 0U ? payload[5] : 0U;
    protocol->target.error_x = protocol->target.has_target != 0U ?
                               (int16_t)((uint16_t)payload[1] |
                                         ((uint16_t)payload[2] << 8U)) : 0;
    protocol->target.error_y = protocol->target.has_target != 0U ?
                               (int16_t)((uint16_t)payload[3] |
                                         ((uint16_t)payload[4] << 8U)) : 0;
    protocol->target.valid = 1U;
    protocol->target.sequence++;
}

static void append(driver_camera_protocol_t *protocol, uint8_t byte)
{
    if (protocol->length < DRIVER_CAMERA_LOGICAL_MAX) {
        protocol->frame[protocol->length++] = byte;
    } else {
        protocol->length = 0U;
        protocol->state = CAMERA_WAIT_HEADER;
    }
}

void driver_camera_protocol_init(driver_camera_protocol_t *protocol)
{
    if (protocol != NULL) {
        (void)memset(protocol, 0, sizeof(*protocol));
    }
}

void driver_camera_protocol_push(driver_camera_protocol_t *protocol,
                                 const uint8_t *data, uint16_t length)
{
    uint16_t i;
    uint8_t byte;

    if ((protocol == NULL) || (data == NULL)) {
        return;
    }
    for (i = 0U; i < length; i++) {
        byte = data[i];
        if (protocol->state == CAMERA_WAIT_HEADER) {
            if (byte == CAMERA_HEADER) {
                protocol->length = 0U;
                protocol->state = CAMERA_RECEIVING;
            }
        } else if (protocol->state == CAMERA_ESCAPE) {
            if (byte == CAMERA_ESCAPE_HEADER) {
                append(protocol, CAMERA_HEADER);
            } else if (byte == CAMERA_ESCAPE_TAIL) {
                append(protocol, CAMERA_TAIL);
            } else if (byte == CAMERA_ESCAPE_ESCAPE) {
                append(protocol, CAMERA_ESCAPE_BYTE);
            } else {
                protocol->state = CAMERA_WAIT_HEADER;
                protocol->length = 0U;
                continue;
            }
            protocol->state = CAMERA_RECEIVING;
        } else if (byte == CAMERA_ESCAPE_BYTE) {
            protocol->state = CAMERA_ESCAPE;
        } else if (byte == CAMERA_TAIL) {
            process_frame(protocol);
            protocol->state = CAMERA_WAIT_HEADER;
            protocol->length = 0U;
        } else if (byte == CAMERA_HEADER) {
            protocol->length = 0U;
        } else {
            append(protocol, byte);
        }
    }
}

status_code_t driver_camera_protocol_snapshot(const driver_camera_protocol_t *protocol,
                                              driver_camera_target_t *target)
{
    if ((protocol == NULL) || (target == NULL)) {
        return STATUS_INVALID_ARGUMENT;
    }
    *target = protocol->target;
    return protocol->target.valid != 0U ? STATUS_OK : STATUS_UNAVAILABLE;
}

status_code_t driver_camera_protocol_encode_switch(uint8_t enabled, uint8_t request_id,
                                                   uint8_t *frame, uint16_t capacity,
                                                   uint16_t *length)
{
    uint8_t logical[6];
    uint8_t i;
    uint16_t output = 0U;

    if ((frame == NULL) || (length == NULL) || (capacity < 8U)) {
        return STATUS_INVALID_ARGUMENT;
    }
    logical[0] = CAMERA_VERSION;
    logical[1] = CAMERA_SWITCH_COMMAND;
    logical[2] = 2U;
    logical[3] = enabled != 0U ? 1U : 0U;
    logical[4] = request_id;
    logical[5] = crc8(logical, 5U);
    frame[output++] = CAMERA_HEADER;
    for (i = 0U; i < sizeof(logical); i++) {
        if ((logical[i] == CAMERA_HEADER) || (logical[i] == CAMERA_TAIL) ||
            (logical[i] == CAMERA_ESCAPE_BYTE)) {
            if ((uint16_t)(output + 2U) > capacity) {
                return STATUS_OUT_OF_RANGE;
            }
            frame[output++] = CAMERA_ESCAPE_BYTE;
            frame[output++] = logical[i] == CAMERA_HEADER ? CAMERA_ESCAPE_HEADER :
                              (logical[i] == CAMERA_TAIL ? CAMERA_ESCAPE_TAIL :
                                                          CAMERA_ESCAPE_ESCAPE);
        } else {
            if ((uint16_t)(output + 1U) > capacity) {
                return STATUS_OUT_OF_RANGE;
            }
            frame[output++] = logical[i];
        }
    }
    frame[output++] = CAMERA_TAIL;
    *length = output;
    return STATUS_OK;
}
