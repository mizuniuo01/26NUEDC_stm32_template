#ifndef AUTO_BALL_CAR_USER_SERVICES_MENU_ACTION_SERVICE_H
#define AUTO_BALL_CAR_USER_SERVICES_MENU_ACTION_SERVICE_H /* 头文件保护 */

#include "menu_service.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#define MENU_ACTION_SERVICE_BINDING_CAPACITY 2U /* 当前支持的主/次动作绑定数 */

/* 由 Application 实现的非阻塞菜单动作回调 */
typedef status_code_t (*menu_action_handler_t)(void *context);

/* 一个逻辑动作与项目功能回调的静态绑定 */
typedef struct {
    menu_service_live_action_t action; /* 仅允许单个主动作或次动作 */
    menu_action_handler_t handler;     /* 非阻塞项目功能回调 */
    void *context;                     /* 回调上下文，生命周期覆盖服务 */
} menu_action_binding_t;

/* 菜单动作扩展服务的初始化配置 */
typedef struct {
    menu_service_t *menu;                    /* 提供 Live 动作的菜单实例 */
    const menu_action_binding_t *bindings;   /* 借用的静态动作绑定表 */
    size_t binding_count;                    /* bindings 的真实元素数 */
} menu_action_service_config_t;

/* 菜单动作扩展服务实例，调用者静态分配并禁止直接修改成员 */
typedef struct {
    menu_action_service_config_t config; /* 初始化时复制的配置 */
    bool is_initialized;                 /* 服务初始化完成标志 */
} menu_action_service_t;

/* 生命周期和非阻塞事件分派接口 */
status_code_t menu_action_service_init(menu_action_service_t *service,
    const menu_action_service_config_t *config);
status_code_t menu_action_service_process(menu_action_service_t *service);

#endif /* AUTO_BALL_CAR_USER_SERVICES_MENU_ACTION_SERVICE_H */
