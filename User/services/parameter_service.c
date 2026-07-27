/**
 * @file parameter_service.c
 * @brief 统一校验和路由可调参数的强类型参数服务。
 * @note 本服务管理稳定 ID、类型、范围、步长和读写权限，不取代 Domain 或
 *       Service 组件对实际参数的所有权。
 * @note 调用者必须在系统组合阶段传入静态分组表。服务只借用分组、字符串、
 *       回调和 context，它们的生命周期必须覆盖 parameter_service_t。
 * @note 数组数量应在定义数组的同一个翻译单元中用 sizeof(array) / sizeof(array[0])
 *       推导，禁止另外维护一份人工计数。
 * @note 典型使用流程为：组件提供类型化 get/apply API，Application 编写窄回调，
 *       再将参数描述符数组一次性传给 parameter_service_init()。
 * @warning 所有公共接口只能在任务或主循环上下文调用，未实现 ISR 安全和多线程锁。
 */
#include "parameter_service.h"
#include <math.h>

/**
 * @brief 判断标量类型是否由参数服务支持
 * @param type 待检查的标量类型
 * @retval true 类型受支持
 * @retval false 类型超出支持范围
 */
static bool parameter_service_type_is_valid(scalar_value_type_t type)
{
    return (type == SCALAR_VALUE_FLOAT) || (type == SCALAR_VALUE_INT32) ||
           (type == SCALAR_VALUE_UINT32) || (type == SCALAR_VALUE_BOOL);
}

/**
 * @brief 检查一个标量的类型与数值有效性
 * @param value 待检查标量
 * @param type 期望的标量类型
 * @retval true 类型匹配且数值可用
 * @retval false 指针为空、类型不匹配或浮点值非有限
 */
static bool parameter_service_value_is_valid(const scalar_value_t *value,
    scalar_value_type_t type)
{
    if (!value || (value->type != type)) {
        return false;
    }
    return (type != SCALAR_VALUE_FLOAT) || isfinite(value->data.float_value);
}

/**
 * @brief 比较两个同类型标量的大小
 * @param left 左操作数
 * @param right 右操作数
 * @return left 小于 right 时返回 -1，相等时返回 0，大于时返回 1
 */
static int parameter_service_compare(const scalar_value_t *left, const scalar_value_t *right)
{
    switch (left->type) {
        case SCALAR_VALUE_FLOAT:
            return (left->data.float_value > right->data.float_value) -
                   (left->data.float_value < right->data.float_value);

        case SCALAR_VALUE_INT32:
            return (left->data.int32_value > right->data.int32_value) -
                   (left->data.int32_value < right->data.int32_value);

        case SCALAR_VALUE_UINT32:
            return (left->data.uint32_value > right->data.uint32_value) -
                   (left->data.uint32_value < right->data.uint32_value);

        case SCALAR_VALUE_BOOL:
            return (left->data.bool_value > right->data.bool_value) -
                   (left->data.bool_value < right->data.bool_value);

        default:
            return 0;
    }
}

/**
 * @brief 检查数值是否位于描述符声明的闭区间
 * @param descriptor 参数描述符
 * @param value 待检查标量
 * @retval true 值在闭区间内
 * @retval false 值超出闭区间
 */
static bool parameter_service_value_is_in_range(
    const parameter_service_descriptor_t *descriptor, const scalar_value_t *value)
{
    return (parameter_service_compare(value, &descriptor->minimum) >= 0) &&
           (parameter_service_compare(value, &descriptor->maximum) <= 0);
}

/**
 * @brief 检查单个参数描述符的静态元数据
 * @param descriptor 待检查参数描述符
 * @retval STATUS_OK 描述符有效
 * @retval STATUS_INVALID_ARGUMENT 名称、单位、回调、类型或数值元数据无效
 */
static status_code_t parameter_service_validate_descriptor(
    const parameter_service_descriptor_t *descriptor)
{
    if (!descriptor || !descriptor->name || (descriptor->name[0] == '\0') ||
        !descriptor->unit || !descriptor->read ||
        (descriptor->is_writable && !descriptor->write) ||
        (!descriptor->is_writable && descriptor->write) ||
        !parameter_service_type_is_valid(descriptor->type) ||
        (descriptor->decimals > PARAMETER_SERVICE_DECIMAL_CAPACITY)) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!parameter_service_value_is_valid(&descriptor->minimum, descriptor->type) ||
        !parameter_service_value_is_valid(&descriptor->maximum, descriptor->type) ||
        !parameter_service_value_is_valid(&descriptor->step, descriptor->type) ||
        (parameter_service_compare(&descriptor->minimum, &descriptor->maximum) > 0)) {
        return STATUS_INVALID_ARGUMENT;
    }
    switch (descriptor->type) {
        case SCALAR_VALUE_FLOAT:
            if (descriptor->step.data.float_value <= 0.0F) {
                return STATUS_INVALID_ARGUMENT;
            }
            break;

        case SCALAR_VALUE_INT32:
            if (descriptor->step.data.int32_value <= 0) {
                return STATUS_INVALID_ARGUMENT;
            }
            break;

        case SCALAR_VALUE_UINT32:
            if (descriptor->step.data.uint32_value == 0U) {
                return STATUS_INVALID_ARGUMENT;
            }
            break;

        case SCALAR_VALUE_BOOL:
            if (!descriptor->step.data.bool_value || descriptor->decimals != 0U) {
                return STATUS_INVALID_ARGUMENT;
            }
            break;

        default:
            return STATUS_INVALID_ARGUMENT;
    }
    return STATUS_OK;
}

/**
 * @brief 检查已扫描参数中是否存在重复稳定 ID
 * @param groups 待检查分组数组
 * @param group_index 当前参数所在分组索引
 * @param parameter_index 当前参数在分组中的索引
 * @param id 当前参数 ID
 * @retval true 之前的参数中已存在相同 ID
 * @retval false 当前 ID 尚未出现
 */
static bool parameter_service_parameter_id_exists(const parameter_service_group_t *groups,
    size_t group_index, size_t parameter_index, uint16_t id)
{
    size_t earlier_group_index;
    size_t earlier_parameter_index;

    for (earlier_group_index = 0U; earlier_group_index <= group_index;
         earlier_group_index++) {
        size_t limit = groups[earlier_group_index].parameter_count;

        if (earlier_group_index == group_index) {
            limit = parameter_index;
        }
        for (earlier_parameter_index = 0U; earlier_parameter_index < limit;
             earlier_parameter_index++) {
            if (groups[earlier_group_index].parameters[earlier_parameter_index].id == id) {
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief 按全局稳定 ID 查找参数所在索引
 * @param service 已初始化的参数服务
 * @param parameter_id 待查找的参数稳定 ID
 * @param group_index 接收分组索引的存储地址
 * @param parameter_index 接收组内参数索引的存储地址
 * @retval STATUS_OK 两个索引已写入
 * @retval STATUS_INVALID_ARGUMENT 任一必需参数为空
 * @retval STATUS_NOT_INITIALIZED 服务尚未初始化
 * @retval STATUS_OUT_OF_RANGE parameter_id 未注册
 */
static status_code_t parameter_service_find_indices(const parameter_service_t *service,
    uint16_t parameter_id, size_t *group_index, size_t *parameter_index)
{
    size_t current_group_index;
    size_t current_parameter_index;

    if (!service || !group_index || !parameter_index) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!service->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    for (current_group_index = 0U; current_group_index < service->group_count;
         current_group_index++) {
        for (current_parameter_index = 0U;
             current_parameter_index < service->groups[current_group_index].parameter_count;
             current_parameter_index++) {
            const parameter_service_descriptor_t *descriptor =
                &service->groups[current_group_index].parameters[current_parameter_index];

            if (descriptor->id == parameter_id) {
                *group_index = current_group_index;
                *parameter_index = current_parameter_index;
                return STATUS_OK;
            }
        }
    }
    return STATUS_OUT_OF_RANGE;
}

/**
 * @brief 初始化一个参数服务并验证全部静态注册项
 * @param service 调用者持有的参数服务实例
 * @param groups 生命周期覆盖 service 的静态分组数组
 * @param group_count groups 中的真实元素数
 * @retval STATUS_OK 注册项全部有效且服务已就绪
 * @retval STATUS_INVALID_ARGUMENT 参数为空、计数为零或元数据非法
 * @retval STATUS_OUT_OF_RANGE 分组或参数数超出服务容量
 * @retval STATUS_STATE_ERROR 稳定 ID 重复或参数所有者返回非法初值
 */
status_code_t parameter_service_init(parameter_service_t *service,
    const parameter_service_group_t *groups, size_t group_count)
{
    scalar_value_t initial_value;
    size_t group_index;
    size_t parameter_index;
    size_t total_count = 0U;
    status_code_t status;

    if (!service || !groups || (group_count == 0U)) {
        return STATUS_INVALID_ARGUMENT;
    }
    service->is_initialized = false;
    if (group_count > PARAMETER_SERVICE_GROUP_CAPACITY) {
        return STATUS_OUT_OF_RANGE;
    }
    for (group_index = 0U; group_index < group_count; group_index++) {
        if (!groups[group_index].name || (groups[group_index].name[0] == '\0') ||
            !groups[group_index].parameters || (groups[group_index].parameter_count == 0U)) {
            return STATUS_INVALID_ARGUMENT;
        }
        for (size_t earlier_group = 0U; earlier_group < group_index; earlier_group++) {
            if (groups[earlier_group].id == groups[group_index].id) {
                return STATUS_STATE_ERROR;
            }
        }
        if (groups[group_index].parameter_count >
            (PARAMETER_SERVICE_PARAMETER_CAPACITY - total_count)) {
            return STATUS_OUT_OF_RANGE;
        }
        for (parameter_index = 0U;
             parameter_index < groups[group_index].parameter_count; parameter_index++) {
            const parameter_service_descriptor_t *descriptor =
                &groups[group_index].parameters[parameter_index];

            status = parameter_service_validate_descriptor(descriptor);
            if (status != STATUS_OK) {
                return status;
            }
            if (parameter_service_parameter_id_exists(groups, group_index, parameter_index,
                    descriptor->id)) {
                return STATUS_STATE_ERROR;
            }
            status = descriptor->read(descriptor->context, &initial_value);
            if ((status != STATUS_OK) ||
                !parameter_service_value_is_valid(&initial_value, descriptor->type) ||
                !parameter_service_value_is_in_range(descriptor, &initial_value)) {
                return STATUS_STATE_ERROR;
            }
        }
        total_count += groups[group_index].parameter_count;
    }
    service->groups = groups;
    service->group_count = group_count;
    service->parameter_count = total_count;
    service->is_initialized = true;
    return STATUS_OK;
}

/**
 * @brief 获取一个参数分组的只读视图
 * @param service 已初始化的参数服务
 * @param group_index 分组索引
 * @param group 接收借用视图的存储地址
 * @retval STATUS_OK 视图已写入
 * @retval STATUS_INVALID_ARGUMENT service 或 group 为空
 * @retval STATUS_NOT_INITIALIZED 服务尚未初始化
 * @retval STATUS_OUT_OF_RANGE group_index 超出注册范围
 */
status_code_t parameter_service_get_group(const parameter_service_t *service,
    size_t group_index, const parameter_service_group_t **group)
{
    if (!service || !group) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!service->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    if (group_index >= service->group_count) {
        return STATUS_OUT_OF_RANGE;
    }
    *group = &service->groups[group_index];
    return STATUS_OK;
}

/**
 * @brief 获取一个参数描述符的只读视图
 * @param service 已初始化的参数服务
 * @param group_index 分组索引
 * @param parameter_index 组内参数索引
 * @param descriptor 接收借用视图的存储地址
 * @retval STATUS_OK 视图已写入
 * @retval STATUS_INVALID_ARGUMENT service 或 descriptor 为空
 * @retval STATUS_NOT_INITIALIZED 服务尚未初始化
 * @retval STATUS_OUT_OF_RANGE 任一索引超出注册范围
 */
status_code_t parameter_service_get_descriptor(const parameter_service_t *service,
    size_t group_index, size_t parameter_index,
    const parameter_service_descriptor_t **descriptor)
{
    const parameter_service_group_t *group;
    status_code_t status;

    if (!descriptor) {
        return STATUS_INVALID_ARGUMENT;
    }
    status = parameter_service_get_group(service, group_index, &group);
    if (status != STATUS_OK) {
        return status;
    }
    if (parameter_index >= group->parameter_count) {
        return STATUS_OUT_OF_RANGE;
    }
    *descriptor = &group->parameters[parameter_index];
    return STATUS_OK;
}

/**
 * @brief 经参数所有者回调读取一个当前值
 * @param service 已初始化的参数服务
 * @param group_index 分组索引
 * @param parameter_index 组内参数索引
 * @param value 接收当前值的存储地址
 * @retval STATUS_OK 值已写入
 * @retval STATUS_INVALID_ARGUMENT 必需参数为空或所有者返回错误类型
 * @retval STATUS_NOT_INITIALIZED 服务尚未初始化
 * @retval STATUS_OUT_OF_RANGE 索引超界或所有者当前值超出注册范围
 * @retval STATUS_STATE_ERROR 参数所有者读取回调报错
 */
status_code_t parameter_service_get(const parameter_service_t *service, size_t group_index,
    size_t parameter_index, scalar_value_t *value)
{
    const parameter_service_descriptor_t *descriptor;
    status_code_t status;

    if (!value) {
        return STATUS_INVALID_ARGUMENT;
    }
    status = parameter_service_get_descriptor(service, group_index, parameter_index, &descriptor);
    if (status != STATUS_OK) {
        return status;
    }
    status = descriptor->read(descriptor->context, value);
    if (status != STATUS_OK) {
        return status;
    }
    if (!parameter_service_value_is_valid(value, descriptor->type)) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!parameter_service_value_is_in_range(descriptor, value)) {
        return STATUS_OUT_OF_RANGE;
    }
    return STATUS_OK;
}

/**
 * @brief 校验并经所有者回调整体应用一个参数值
 * @param service 已初始化的参数服务
 * @param group_index 分组索引
 * @param parameter_index 组内参数索引
 * @param value 待应用的强类型值
 * @retval STATUS_OK 所有者已接受新值
 * @retval STATUS_INVALID_ARGUMENT value 为空或类型不匹配
 * @retval STATUS_NOT_INITIALIZED 服务尚未初始化
 * @retval STATUS_OUT_OF_RANGE 索引或数值超出注册范围
 * @retval STATUS_STATE_ERROR 参数是只读项
 * @retval STATUS_IO_ERROR 所有者在应用时报告底层错误
 */
status_code_t parameter_service_set(parameter_service_t *service, size_t group_index,
    size_t parameter_index, const scalar_value_t *value)
{
    const parameter_service_descriptor_t *descriptor;
    status_code_t status;

    status = parameter_service_get_descriptor(service, group_index, parameter_index, &descriptor);
    if (status != STATUS_OK) {
        return status;
    }
    if (!parameter_service_value_is_valid(value, descriptor->type)) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!descriptor->is_writable) {
        return STATUS_STATE_ERROR;
    }
    if (!parameter_service_value_is_in_range(descriptor, value)) {
        return STATUS_OUT_OF_RANGE;
    }
    return descriptor->write(descriptor->context, value);
}

/**
 * @brief 按注册步长调整一个可写参数并在边界处钳制
 * @param service 已初始化的参数服务
 * @param group_index 分组索引
 * @param parameter_index 组内参数索引
 * @param step_count 有符号步数，正数增大、负数减小
 * @retval STATUS_OK 调整后的值已由所有者接受
 * @retval STATUS_INVALID_ARGUMENT 值或元数据类型不一致
 * @retval STATUS_NOT_INITIALIZED 服务尚未初始化
 * @retval STATUS_OUT_OF_RANGE 索引超出注册范围
 * @retval STATUS_STATE_ERROR 参数是只读项
 */
status_code_t parameter_service_adjust(parameter_service_t *service, size_t group_index,
    size_t parameter_index, int32_t step_count)
{
    const parameter_service_descriptor_t *descriptor;
    scalar_value_t value;
    int64_t adjusted;
    double adjusted_float;
    status_code_t status;

    status = parameter_service_get_descriptor(service, group_index, parameter_index, &descriptor);
    if (status != STATUS_OK) {
        return status;
    }
    if (!descriptor->is_writable) {
        return STATUS_STATE_ERROR;
    }
    status = parameter_service_get(service, group_index, parameter_index, &value);
    if (status != STATUS_OK) {
        return status;
    }
    switch (descriptor->type) {
        case SCALAR_VALUE_FLOAT:
            adjusted_float = (double)value.data.float_value +
                             (double)descriptor->step.data.float_value * (double)step_count;
            if (adjusted_float < (double)descriptor->minimum.data.float_value) {
                value = descriptor->minimum;
            } else if (adjusted_float > (double)descriptor->maximum.data.float_value) {
                value = descriptor->maximum;
            } else {
                value.data.float_value = (float)adjusted_float;
            }
            break;

        case SCALAR_VALUE_INT32:
            adjusted = (int64_t)value.data.int32_value +
                       (int64_t)descriptor->step.data.int32_value * (int64_t)step_count;
            if (adjusted < (int64_t)descriptor->minimum.data.int32_value) {
                value = descriptor->minimum;
            } else if (adjusted > (int64_t)descriptor->maximum.data.int32_value) {
                value = descriptor->maximum;
            } else {
                value.data.int32_value = (int32_t)adjusted;
            }
            break;

        case SCALAR_VALUE_UINT32:
            adjusted = (int64_t)value.data.uint32_value +
                       (int64_t)descriptor->step.data.uint32_value * (int64_t)step_count;
            if (adjusted < (int64_t)descriptor->minimum.data.uint32_value) {
                value = descriptor->minimum;
            } else if (adjusted > (int64_t)descriptor->maximum.data.uint32_value) {
                value = descriptor->maximum;
            } else {
                value.data.uint32_value = (uint32_t)adjusted;
            }
            break;

        case SCALAR_VALUE_BOOL:
            if (step_count != 0) {
                value.data.bool_value = step_count > 0;
            }
            break;

        default:
            return STATUS_INVALID_ARGUMENT;
    }
    return parameter_service_set(service, group_index, parameter_index, &value);
}

/**
 * @brief 按全局稳定 ID 读取一个参数
 * @param service 已初始化的参数服务
 * @param parameter_id 已注册的参数稳定 ID
 * @param value 接收当前值的存储地址
 * @retval STATUS_OK 值已写入
 * @retval STATUS_INVALID_ARGUMENT 必需参数为空或所有者返回错误类型
 * @retval STATUS_NOT_INITIALIZED 服务尚未初始化
 * @retval STATUS_OUT_OF_RANGE ID 未注册或所有者当前值越界
 */
status_code_t parameter_service_get_by_id(const parameter_service_t *service,
    uint16_t parameter_id, scalar_value_t *value)
{
    size_t group_index;
    size_t parameter_index;
    status_code_t status;

    status = parameter_service_find_indices(service, parameter_id, &group_index,
        &parameter_index);
    if (status != STATUS_OK) {
        return status;
    }
    return parameter_service_get(service, group_index, parameter_index, value);
}

/**
 * @brief 按全局稳定 ID 校验并应用一个参数
 * @param service 已初始化的参数服务
 * @param parameter_id 已注册的参数稳定 ID
 * @param value 待应用的强类型值
 * @retval STATUS_OK 所有者已接受新值
 * @retval STATUS_INVALID_ARGUMENT value 为空或类型不匹配
 * @retval STATUS_NOT_INITIALIZED 服务尚未初始化
 * @retval STATUS_OUT_OF_RANGE ID 未注册或数值超出范围
 * @retval STATUS_STATE_ERROR 参数是只读项
 */
status_code_t parameter_service_set_by_id(parameter_service_t *service, uint16_t parameter_id,
    const scalar_value_t *value)
{
    size_t group_index;
    size_t parameter_index;
    status_code_t status;

    status = parameter_service_find_indices(service, parameter_id, &group_index,
        &parameter_index);
    if (status != STATUS_OK) {
        return status;
    }
    return parameter_service_set(service, group_index, parameter_index, value);
}

/**
 * @brief 按全局稳定 ID 使用注册步长调整一个参数
 * @param service 已初始化的参数服务
 * @param parameter_id 已注册的参数稳定 ID
 * @param step_count 有符号步数，正数增大、负数减小
 * @retval STATUS_OK 调整后的值已由所有者接受
 * @retval STATUS_INVALID_ARGUMENT 值或元数据类型不一致
 * @retval STATUS_NOT_INITIALIZED 服务尚未初始化
 * @retval STATUS_OUT_OF_RANGE ID 未注册
 * @retval STATUS_STATE_ERROR 参数是只读项
 */
status_code_t parameter_service_adjust_by_id(parameter_service_t *service,
    uint16_t parameter_id, int32_t step_count)
{
    size_t group_index;
    size_t parameter_index;
    status_code_t status;

    status = parameter_service_find_indices(service, parameter_id, &group_index,
        &parameter_index);
    if (status != STATUS_OK) {
        return status;
    }
    return parameter_service_adjust(service, group_index, parameter_index, step_count);
}
