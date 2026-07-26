/**
 * @file command_service.c
 * @brief Framed command parser implementation.
 */
#include "command_service.h"

#include <string.h>

static void command_service_dispatch(command_service_t *service)
{
    char *separator;
    uint8_t i;

    service->frame[service->length] = '\0';
    separator = strchr(service->frame, '=');
    if (separator != NULL) {
        *separator = '\0';
    }
    for (i = 0U; i < service->binding_count; i++) {
        if (strcmp(service->frame, service->bindings[i].name) == 0) {
            service->bindings[i].callback(service->frame,
                                          separator == NULL ? "" : separator + 1,
                                          service->bindings[i].context);
            break;
        }
    }
}

void command_service_init(command_service_t *service)
{
    if (service != NULL) {
        (void)memset(service, 0, sizeof(*service));
    }
}

int command_service_bind(command_service_t *service, const char *name,
                         command_callback_t callback, void *context)
{
    command_binding_t *binding;

    if ((service == NULL) || (name == NULL) || (callback == NULL) ||
        (service->binding_count >= COMMAND_SERVICE_MAX_CALLBACKS)) {
        return -1;
    }
    binding = &service->bindings[service->binding_count++];
    (void)strncpy(binding->name, name, sizeof(binding->name) - 1U);
    binding->name[sizeof(binding->name) - 1U] = '\0';
    binding->callback = callback;
    binding->context = context;
    return 0;
}

void command_service_push(command_service_t *service, const uint8_t *data, uint16_t length)
{
    uint16_t i;

    if ((service == NULL) || (data == NULL)) {
        return;
    }
    for (i = 0U; i < length; i++) {
        if (data[i] == '@') {
            service->in_frame = 1U;
            service->length = 0U;
        } else if ((data[i] == '#') && (service->in_frame != 0U)) {
            command_service_dispatch(service);
            service->in_frame = 0U;
            service->length = 0U;
        } else if ((service->in_frame != 0U) &&
                   (service->length < (COMMAND_SERVICE_MAX_FRAME - 1U))) {
            service->frame[service->length++] = (char)data[i];
        } else if (service->length >= (COMMAND_SERVICE_MAX_FRAME - 1U)) {
            service->in_frame = 0U;
            service->length = 0U;
        }
    }
}
