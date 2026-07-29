/**
 * @file menu_action_service.c
 * @brief 将菜单 Live 逻辑动作分派给 Application 静态注册的项目回调。
 * @note 本服务只依赖 Menu Service 公共事件接口，不读取 BSP 按键，也不认识具体执行器。
 * @note 绑定表只在初始化时注册并由调用者长期持有；运行期不支持替换或注销。
 * @note 每次 process 最多处理主、次两个动作。回调失败不触发自动重试，也不阻止本轮
 *       后续动作执行；函数最终返回遇到的第一个错误。
 * @warning 回调在协作式主循环上下文同步执行，必须非阻塞且工作量有界。
 */
#include "menu_action_service.h"

/**
 * @brief 判断一个动作值是否为可注册的单一逻辑动作
 * @param action 待验证动作
 * @retval true action 是主动作或次动作
 * @retval false action 为 NONE、组合掩码或未知值
 */
static bool menu_action_service_action_is_valid(menu_service_live_action_t action)
{
    return (action == MENU_SERVICE_LIVE_ACTION_PRIMARY) ||
           (action == MENU_SERVICE_LIVE_ACTION_SECONDARY);
}

/**
 * @brief 查找一个已注册动作的静态绑定
 * @param service 已初始化菜单动作服务
 * @param action 待查找的单一动作
 * @return 对应绑定；动作未注册时返回 NULL
 */
static const menu_action_binding_t *menu_action_service_find_binding(
    const menu_action_service_t *service, menu_service_live_action_t action)
{
    size_t binding_index;

    for (binding_index = 0U; binding_index < service->config.binding_count; binding_index++) {
        if (service->config.bindings[binding_index].action == action) {
            return &service->config.bindings[binding_index];
        }
    }
    return NULL;
}

/**
 * @brief 初始化菜单动作扩展服务并验证静态绑定表
 * @param service 由调用者静态持有的服务实例
 * @param config 菜单实例和静态绑定表配置
 * @retval STATUS_OK 服务初始化完成
 * @retval STATUS_INVALID_ARGUMENT 实例、菜单、数量、动作或回调无效
 * @retval STATUS_NOT_INITIALIZED 菜单尚未初始化
 * @retval STATUS_OUT_OF_RANGE 绑定数量超过固定容量
 * @retval STATUS_STATE_ERROR 同一动作被重复注册
 */
status_code_t menu_action_service_init(menu_action_service_t *service,
    const menu_action_service_config_t *config)
{
    size_t binding_index;
    size_t earlier_index;

    if (!service) {
        return STATUS_INVALID_ARGUMENT;
    }
    service->is_initialized = false;
    if (!config || !config->menu ||
        ((config->binding_count > 0U) && !config->bindings)) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!config->menu->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (config->binding_count > MENU_ACTION_SERVICE_BINDING_CAPACITY) {
        return STATUS_OUT_OF_RANGE;
    }
    for (binding_index = 0U; binding_index < config->binding_count; binding_index++) {
        const menu_action_binding_t *binding = &config->bindings[binding_index];

        if (!menu_action_service_action_is_valid(binding->action) || !binding->handler) {
            return STATUS_INVALID_ARGUMENT;
        }
        for (earlier_index = 0U; earlier_index < binding_index; earlier_index++) {
            if (config->bindings[earlier_index].action == binding->action) {
                return STATUS_STATE_ERROR;
            }
        }
    }
    service->config = *config;
    service->is_initialized = true;
    return STATUS_OK;
}

/**
 * @brief 取走并分派一轮菜单 Live 逻辑动作
 * @param service 已初始化菜单动作扩展服务
 * @retval STATUS_OK 没有动作、动作未绑定或全部回调成功
 * @retval STATUS_INVALID_ARGUMENT service 为空
 * @retval STATUS_NOT_INITIALIZED 服务或关联菜单尚未初始化
 * @return 回调失败时返回本轮按主、次顺序遇到的第一个错误
 */
status_code_t menu_action_service_process(menu_action_service_t *service)
{
    static const menu_service_live_action_t action_order[] = {
        MENU_SERVICE_LIVE_ACTION_PRIMARY,
        MENU_SERVICE_LIVE_ACTION_SECONDARY,
    };
    status_code_t first_status = STATUS_OK;
    uint8_t pending_actions;
    size_t action_index;
    status_code_t status;

    if (!service) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!service->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    status = menu_service_take_live_actions(service->config.menu, &pending_actions);
    if (status != STATUS_OK) {
        return status;
    }
    for (action_index = 0U; action_index < sizeof(action_order) / sizeof(action_order[0]);
         action_index++) {
        const menu_service_live_action_t action = action_order[action_index];
        const menu_action_binding_t *binding;

        if ((pending_actions & (uint8_t)action) == 0U) {
            continue;
        }
        binding = menu_action_service_find_binding(service, action);
        if (!binding) {
            continue;
        }
        status = binding->handler(binding->context);
        if ((status != STATUS_OK) && (first_status == STATUS_OK)) {
            first_status = status;
        }
    }
    return first_status;
}
