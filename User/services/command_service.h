/**
 * @file command_service.h
 * @brief Transport-independent framed command parser and callback registry.
 */
#ifndef USER_SERVICES_COMMAND_SERVICE_H
#define USER_SERVICES_COMMAND_SERVICE_H

#include <stdint.h>

#define COMMAND_SERVICE_MAX_FRAME 96U
#define COMMAND_SERVICE_MAX_CALLBACKS 16U

typedef void (*command_callback_t)(const char *name, const char *value, void *context);

typedef struct {
    char name[24];
    command_callback_t callback;
    void *context;
} command_binding_t;

typedef struct {
    char frame[COMMAND_SERVICE_MAX_FRAME];
    uint8_t length;
    uint8_t in_frame;
    command_binding_t bindings[COMMAND_SERVICE_MAX_CALLBACKS];
    uint8_t binding_count;
} command_service_t;

void command_service_init(command_service_t *service);
int command_service_bind(command_service_t *service, const char *name,
                         command_callback_t callback, void *context);
void command_service_push(command_service_t *service, const uint8_t *data, uint16_t length);

#endif
