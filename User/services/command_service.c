/**
 * @file command_service.c
 * @brief 解析文本命令帧并分派到按名称注册的处理回调。
 */
#include "command_service.h"
#include <string.h>

/**
 * @brief  解析当前完整命令帧并调用第一个同名回调
 * @param  service 已完成一帧接收的命令服务实例
 */
static void command_service_dispatch(command_service_t *service)
{
    char *separator;
    uint8_t i;

    service->frame[service->length] = '\0';
    separator = strchr(service->frame, '=');
    if (separator) {
        *separator = '\0';
    }
    for (i = 0U; i < service->binding_count; i++) {
        if (strcmp(service->frame, service->bindings[i].name) == 0) {
            service->bindings[i].callback(service->frame, separator ? separator + 1 : "",
                service->bindings[i].context);
            break;
        }
    }
}

/**
 * @brief  初始化文本命令服务和回调绑定表
 * @param  service 命令服务实例，为空时不执行操作
 */
void command_service_init(command_service_t *service)
{
    if (service) {
        (void)memset(service, 0, sizeof(*service));
    }
}

/**
 * @brief  将命令名称绑定到处理回调
 * @param  service 命令服务实例
 * @param  name 以空字符结尾的命令名称
 * @param  callback 收到同名命令时调用的处理函数
 * @param  context 调用回调时透传的上下文
 * @retval STATUS_OK 回调绑定成功
 * @retval STATUS_INVALID_ARGUMENT service、name 或 callback 为空
 * @retval STATUS_OUT_OF_RANGE 回调绑定表已满
 */
status_code_t command_service_bind(command_service_t *service, const char *name,
    command_callback_t callback, void *context)
{
    command_binding_t *binding;

    if (!service || !name || !callback) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (service->binding_count >= COMMAND_SERVICE_CALLBACK_CAPACITY) {
        return STATUS_OUT_OF_RANGE;
    }
    binding = &service->bindings[service->binding_count++];
    (void)strncpy(binding->name, name, sizeof(binding->name) - 1U);
    binding->name[sizeof(binding->name) - 1U] = '\0';
    binding->callback = callback;
    binding->context = context;
    return STATUS_OK;
}

/**
 * @brief  向命令服务提交一段可能包含多帧的字节流
 * @param  service 命令服务实例
 * @param  data 输入字节序列
 * @param  length 输入字节数
 */
void command_service_push(command_service_t *service, const uint8_t *data, uint16_t length)
{
    uint16_t i;

    if (!service || !data) {
        return;
    }
    for (i = 0U; i < length; i++) {
        if (data[i] == '@') {
            service->is_in_frame = true;
            service->length = 0U;
        } else if ((data[i] == '#') && service->is_in_frame) {
            command_service_dispatch(service);
            service->is_in_frame = false;
            service->length = 0U;
        } else if (service->is_in_frame &&
                   (service->length < (COMMAND_SERVICE_FRAME_CAPACITY - 1U))) {
            service->frame[service->length++] = (char)data[i];
        } else if (service->length >= (COMMAND_SERVICE_FRAME_CAPACITY - 1U)) {
            service->is_in_frame = false;
            service->length = 0U;
        }
    }
}
