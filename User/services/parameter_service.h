#ifndef AUTO_BALL_CAR_USER_SERVICES_PARAMETER_SERVICE_H
#define AUTO_BALL_CAR_USER_SERVICES_PARAMETER_SERVICE_H /* 头文件保护 */

#include "scalar_value.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PARAMETER_SERVICE_GROUP_CAPACITY 16U /* 单个服务可注册的最大分组数 */
#define PARAMETER_SERVICE_PARAMETER_CAPACITY 64U /* 单个服务可注册的最大参数数 */
#define PARAMETER_SERVICE_DECIMAL_CAPACITY 4U /* UI 元数据允许的最大小数位数 */

/* 参数读取和写入回调类型 */
typedef status_code_t (*parameter_service_read_t)(void *context, scalar_value_t *value);
typedef status_code_t (*parameter_service_write_t)(void *context, const scalar_value_t *value);

/* 一个可注册运行参数的元数据和所有者绑定 */
typedef struct {
    uint16_t id;                         /* 跨 UI 和通信入口稳定的参数 ID */
    const char *name;                    /* 以空字符结尾的参数名称 */
    const char *unit;                    /* 以空字符结尾的单位，无单位时使用空串 */
    scalar_value_type_t type;            /* 参数值类型 */
    scalar_value_t minimum;              /* 允许提交的最小值 */
    scalar_value_t maximum;              /* 允许提交的最大值 */
    scalar_value_t step;                 /* 默认调节步长 */
    uint8_t decimals;                    /* 浮点显示小数位数 */
    bool is_writable;                    /* 允许通过参数服务写入的标志 */
    parameter_service_read_t read;       /* 从参数所有者读取当前值 */
    parameter_service_write_t write;     /* 向参数所有者整体应用新值 */
    void *context;                       /* 回调上下文，生命周期须覆盖服务 */
} parameter_service_descriptor_t;

/* 一组静态参数描述符 */
typedef struct {
    uint16_t id;                                      /* 稳定的参数分组 ID */
    const char *name;                                 /* 以空字符结尾的分组名称 */
    const parameter_service_descriptor_t *parameters; /* 注册期间借用的参数数组 */
    size_t parameter_count;                           /* parameters 真实元素数 */
} parameter_service_group_t;

/* 参数服务实例，调用者静态分配并持有 */
typedef struct {
    const parameter_service_group_t *groups; /* 借用的静态分组表 */
    size_t group_count;                      /* 已注册分组数 */
    size_t parameter_count;                  /* 所有分组的参数总数 */
    bool is_initialized;                     /* 服务初始化完成标志 */
} parameter_service_t;

/* 生命周期与元数据查询接口 */
status_code_t parameter_service_init(parameter_service_t *service,
    const parameter_service_group_t *groups, size_t group_count);
status_code_t parameter_service_get_group(const parameter_service_t *service,
    size_t group_index, const parameter_service_group_t **group);
status_code_t parameter_service_get_descriptor(const parameter_service_t *service,
    size_t group_index, size_t parameter_index,
    const parameter_service_descriptor_t **descriptor);

/* 参数访问接口 */
status_code_t parameter_service_get(const parameter_service_t *service, size_t group_index,
    size_t parameter_index, scalar_value_t *value);
status_code_t parameter_service_set(parameter_service_t *service, size_t group_index,
    size_t parameter_index, const scalar_value_t *value);
status_code_t parameter_service_adjust(parameter_service_t *service, size_t group_index,
    size_t parameter_index, int32_t step_count);

/* 面向本地 UI、通信协议等统一入口的稳定 ID 访问接口 */
status_code_t parameter_service_get_by_id(const parameter_service_t *service,
    uint16_t parameter_id, scalar_value_t *value);
status_code_t parameter_service_set_by_id(parameter_service_t *service, uint16_t parameter_id,
    const scalar_value_t *value);
status_code_t parameter_service_adjust_by_id(parameter_service_t *service,
    uint16_t parameter_id, int32_t step_count);

#endif /* AUTO_BALL_CAR_USER_SERVICES_PARAMETER_SERVICE_H */
