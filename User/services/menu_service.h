#ifndef AUTO_BALL_CAR_USER_SERVICES_MENU_SERVICE_H
#define AUTO_BALL_CAR_USER_SERVICES_MENU_SERVICE_H /* 头文件保护 */

#include "parameter_service.h"
#include "scalar_value.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MENU_SERVICE_KEY_COUNT 5U /* 菜单交互所需的按键数 */
#define MENU_SERVICE_STEP_CACHE_CAPACITY 64U /* 可记忆运行期步长的最大参数数 */
#define MENU_SERVICE_NOTIFICATION_CAPACITY 22U /* OLED 底部通知文本容量 */

/* OLED 菜单的顶层显示模式 */
typedef enum {
    MENU_SERVICE_MODE_DEBUG = 0, /* 分组浏览和运行期参数编辑 */
    MENU_SERVICE_MODE_LIVE,      /* 只读实时数据滚动显示 */
} menu_service_mode_t;

/* 调试模式的三级页面状态 */
typedef enum {
    MENU_SERVICE_DEBUG_GROUP = 0, /* L1 参数分组列表 */
    MENU_SERVICE_DEBUG_PARAMETER, /* L2 组内参数列表 */
    MENU_SERVICE_DEBUG_EDIT,      /* L3 参数值草稿编辑 */
    MENU_SERVICE_DEBUG_STEP,      /* L3 当前参数步长编辑 */
} menu_service_debug_state_t;

/* 硬件无关菜单所需的最小能力回调 */
typedef status_code_t (*menu_service_time_t)(void *context, uint32_t *now_ms);
typedef status_code_t (*menu_service_keys_t)(void *context, uint8_t *state);
typedef status_code_t (*menu_service_frame_ready_t)(void *context, bool *is_ready);
typedef status_code_t (*menu_service_clear_t)(void *context);
typedef status_code_t (*menu_service_set_pixel_t)(void *context, uint8_t x, uint8_t y,
    bool is_on);
typedef status_code_t (*menu_service_refresh_t)(void *context);

/* 由系统组合根绑定的时间、按键和显示端口 */
typedef struct {
    menu_service_time_t get_time;           /* 读取单调毫秒时间 */
    menu_service_keys_t get_keys;           /* 读取五个按键的稳定位掩码 */
    menu_service_frame_ready_t frame_ready; /* 查询显存是否允许开始新帧 */
    menu_service_clear_t clear;             /* 清空由菜单独占的显存 */
    menu_service_set_pixel_t set_pixel;     /* 写入一个单色像素 */
    menu_service_refresh_t refresh;         /* 非阻塞提交完整显存帧 */
    void *context;                          /* 端口上下文，生命周期覆盖菜单 */
} menu_service_port_t;

/* 实时数据项读取回调 */
typedef status_code_t (*menu_service_live_read_t)(void *context, scalar_value_t *value);

/* 实时模式的一个只读数据项 */
typedef struct {
    uint16_t id;                      /* 稳定的实时数据项 ID */
    const char *name;                 /* 以空字符结尾的简短显示名称 */
    const char *unit;                 /* 以空字符结尾的单位，无单位时使用空串 */
    scalar_value_type_t type;         /* 回调必须返回的标量类型 */
    uint8_t decimals;                 /* 浮点值显示小数位数 */
    menu_service_live_read_t read;    /* 读取最新快照数值 */
    void *context;                    /* 回调上下文，生命周期覆盖菜单 */
} menu_service_live_item_t;

/* 菜单实例的初始化配置 */
typedef struct {
    parameter_service_t *parameters;          /* 由菜单访问的参数服务 */
    const menu_service_live_item_t *live_items; /* 借用的静态实时数据表 */
    size_t live_item_count;                    /* live_items 的真实元素数 */
    menu_service_port_t port;                  /* 由 Application 绑定的最小端口 */
    uint32_t debug_refresh_period_ms;          /* 调试页同步周期，单位：毫秒 */
    uint32_t live_refresh_period_ms;           /* 实时页目标刷新周期，单位：毫秒 */
    uint32_t long_press_ms;                    /* K3 长按阈值，单位：毫秒 */
    uint32_t repeat_delay_ms;                  /* K1/K2 首次连发延时，单位：毫秒 */
    uint32_t repeat_period_ms;                 /* K1/K2 连发周期，单位：毫秒 */
} menu_service_config_t;

/* 供诊断和主机测试读取的菜单状态快照 */
typedef struct {
    menu_service_mode_t mode;              /* 当前显示模式 */
    menu_service_debug_state_t debug_state; /* 调试模式当前级别 */
    size_t group_index;                    /* 当前参数分组索引 */
    size_t parameter_index;                /* 当前组内参数索引 */
    size_t live_scroll;                    /* 实时列表首个可见项索引 */
    scalar_value_t edit_value;             /* 当前未提交草稿 */
    bool has_draft;                        /* 当前处于数值或步长编辑的标志 */
} menu_service_snapshot_t;

/* 菜单服务实例，调用者静态分配并禁止直接修改成员 */
typedef struct {
    menu_service_config_t config; /* 初始化时复制的配置和端口 */
    scalar_value_t step_cache[MENU_SERVICE_STEP_CACHE_CAPACITY]; /* 每个参数的运行期步长 */
    scalar_value_t edit_value;     /* 当前参数草稿 */
    scalar_value_t original_value; /* 进入编辑时的冲突检测基线 */
    scalar_value_t original_step;  /* 进入步长编辑时的取消基线 */
    menu_service_mode_t mode;      /* 当前顶层显示模式 */
    menu_service_debug_state_t debug_state; /* 当前调试页级别 */
    size_t group_index;            /* 当前分组索引 */
    size_t parameter_index;        /* 当前参数索引 */
    size_t group_scroll;           /* 分组列表窗口起点 */
    size_t parameter_scroll;       /* 参数列表窗口起点 */
    size_t live_scroll;            /* 实时列表窗口起点 */
    size_t step_cache_index;       /* 当前参数对应的步长缓存索引 */
    uint32_t pressed_at_ms[MENU_SERVICE_KEY_COUNT]; /* 按键稳定按下起始时间 */
    uint32_t repeat_at_ms[MENU_SERVICE_KEY_COUNT];  /* K1/K2 下一次连发时间 */
    uint32_t next_refresh_ms;       /* 下一次周期刷新截止时间 */
    uint32_t notification_until_ms; /* 底部通知失效时间 */
    char notification[MENU_SERVICE_NOTIFICATION_CAPACITY]; /* 底部短通知 */
    uint8_t previous_key_state;     /* 上一次稳定按键位掩码 */
    uint8_t long_emitted_mask;      /* 已产生长按事件的按键位掩码 */
    bool is_key_chord_blocked;       /* K1–K4 组合键释放前忽略输入的标志 */
    bool is_dirty;                  /* 界面状态变更后等待提交新帧 */
    bool is_initialized;            /* 菜单服务初始化完成标志 */
} menu_service_t;

/* 生命周期、非阻塞处理与查询接口 */
status_code_t menu_service_init(menu_service_t *menu, const menu_service_config_t *config);
status_code_t menu_service_process(menu_service_t *menu);
status_code_t menu_service_get_snapshot(const menu_service_t *menu,
    menu_service_snapshot_t *snapshot);

#endif /* AUTO_BALL_CAR_USER_SERVICES_MENU_SERVICE_H */
