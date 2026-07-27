#ifndef AUTO_BALL_CAR_USER_SERVICES_COMMAND_SERVICE_H
#define AUTO_BALL_CAR_USER_SERVICES_COMMAND_SERVICE_H /* 头文件保护 */

#include "status.h"
#include <stdbool.h>
#include <stdint.h>

#define COMMAND_SERVICE_FRAME_CAPACITY 96U /* 单条文本命令帧的字符容量 */
#define COMMAND_SERVICE_CALLBACK_CAPACITY 16U /* 回调绑定表容量 */

/* 命令回调类型，名称和值仅在回调执行期间有效。 */
typedef void (*command_callback_t)(const char *name, const char *value, void *context);

/* 一个命令名称与处理回调的绑定关系 */
typedef struct {
    char name[24];                 /* 以空字符结尾的命令名称 */
    command_callback_t callback;   /* 收到同名命令时调用的处理函数 */
    void *context;                 /* 调用回调时透传的上下文 */
} command_binding_t;

/* 文本命令解析与回调注册服务实例 */
typedef struct {
    char frame[COMMAND_SERVICE_FRAME_CAPACITY];           /* 当前命令帧缓冲区 */
    uint8_t length;                                       /* 当前帧字符数 */
    bool is_in_frame;                                     /* 正在接收命令帧标志 */
    command_binding_t bindings[COMMAND_SERVICE_CALLBACK_CAPACITY]; /* 回调绑定表 */
    uint8_t binding_count;                                /* 已注册绑定数量 */
} command_service_t;

/* 生命周期、回调注册与字节流解析接口 */
void command_service_init(command_service_t *service);
status_code_t command_service_bind(command_service_t *service, const char *name,
    command_callback_t callback, void *context);
void command_service_push(command_service_t *service, const uint8_t *data, uint16_t length);

#endif /* AUTO_BALL_CAR_USER_SERVICES_COMMAND_SERVICE_H */
