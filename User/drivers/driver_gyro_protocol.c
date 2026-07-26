/**
 * @file driver_gyro_protocol.c
 * @brief WIT 0x55/0x53 angle frame parser implementation.
 */
#include "driver_gyro_protocol.h"

#include <string.h>

static uint8_t checksum_valid(const uint8_t *frame)
{
    uint8_t sum = 0U;
    uint8_t i;

    for (i = 0U; i < 10U; i++) {
        sum = (uint8_t)(sum + frame[i]);
    }
    return sum == frame[10] ? 1U : 0U;
}

static int16_t read_i16(const uint8_t *data)
{
    return (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

void driver_gyro_protocol_init(driver_gyro_protocol_t *protocol)
{
    if (protocol != NULL) {
        (void)memset(protocol, 0, sizeof(*protocol));
    }
}

void driver_gyro_protocol_push(driver_gyro_protocol_t *protocol,
                              const uint8_t *data, uint16_t length)
{
    uint16_t i;

    if ((protocol == NULL) || (data == NULL)) {
        return;
    }
    for (i = 0U; i < length; i++) {
        if ((protocol->index == 0U) && (data[i] != 0x55U)) {
            continue;
        }
        if ((protocol->index == 1U) && (data[i] != 0x53U)) {
            protocol->index = data[i] == 0x55U ? 1U : 0U;
            continue;
        }
        protocol->frame[protocol->index++] = data[i];
        if (protocol->index == sizeof(protocol->frame)) {
            if (checksum_valid(protocol->frame) != 0U) {
                protocol->attitude.roll =
                    (float)read_i16(&protocol->frame[2]) * 180.0f / 32768.0f;
                protocol->attitude.pitch =
                    (float)read_i16(&protocol->frame[4]) * 180.0f / 32768.0f;
                protocol->attitude.yaw =
                    (float)read_i16(&protocol->frame[6]) * 180.0f / 32768.0f;
                protocol->attitude.valid = 1U;
                protocol->attitude.sequence++;
            }
            protocol->index = 0U;
        }
    }
}

status_code_t driver_gyro_protocol_snapshot(const driver_gyro_protocol_t *protocol,
                                            driver_gyro_attitude_t *attitude)
{
    if ((protocol == NULL) || (attitude == NULL)) {
        return STATUS_INVALID_ARGUMENT;
    }
    *attitude = protocol->attitude;
    return protocol->attitude.valid != 0U ? STATUS_OK : STATUS_UNAVAILABLE;
}
