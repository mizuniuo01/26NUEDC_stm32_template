/**
 * @file app.c
 * @brief 组合 AutoBallCar 的板级能力、速度控制器、参数服务和 OLED 菜单。
 * @note 本文件是当前固件的系统 Composition Root。main.c 只负责 CubeMX 平台启动、
 *       启动 TIM6 后调用 app_init()，并在无限循环中调用 app_run_once()。
 * @note app_init() 依次初始化 BSP、唯一速度控制器、Parameter Service 和 Menu Service。
 *       任一步失败都会停止后续初始化并把状态返回 main.c；OLED 自身的异步初始化由
 *       bsp_board_process() 在主循环中继续推进。
 * @note 参数注册采用“静态描述符数组 + sizeof(array) / sizeof(array[0])”形式，数量不
 *       单独手填。每个回调先调用速度控制器类型化 get/apply API，禁止保存或暴露 PID
 *       内部字段指针。共同速度 PID 每次提交都会同步左右速度环。
 * @note 新增参数的步骤：为参数所有者提供类型化 get/apply API；扩展字段上下文和窄回调；
 *       在 speed_pid_parameters 中增加带稳定 ID、类型、范围、步长和单位的描述符。不得
 *       修改 menu_service.c，也不得把参数所有权转移到 Parameter Service。
 * @note 新增实时项的步骤：在 app_live_source_t 增加来源，扩展 app_live_read() 的快照
 *       映射，再在 live_items 中注册名称、单位、类型和小数位。快照无效时返回
 *       STATUS_UNAVAILABLE，菜单会在固定位置显示“--”。
 * @note OLED 和按键只在本文件的端口适配函数中连接 BSP。Menu Service 不知道当前板卡、
 *       引脚、HAL 句柄或器件类型，主机测试可用同一端口契约注入替身。
 * @note app_run_once() 始终先推进 BSP，再推进菜单。菜单错误只在状态发生变化时记录一次，
 *       避免不可用设备在高速主循环中淹没固定容量诊断缓冲区。
 * @warning app_init() 和 app_run_once() 只能在主循环任务上下文调用，不允许从 ISR 调用。
 */
#include "app.h"
#include "bsp_board.h"
#include "error_service.h"
#include "menu_service.h"
#include "parameter_service.h"
#include "speed_controller.h"
#include <stddef.h>

#define APP_DEBUG_REFRESH_PERIOD_MS 250U /* 调试页周期同步时间，单位：毫秒 */
#define APP_LIVE_REFRESH_PERIOD_MS 100U /* 实时页目标刷新时间，单位：毫秒 */
#define APP_MENU_LONG_PRESS_MS 800U /* K3 长按进入步长编辑的阈值，单位：毫秒 */
#define APP_MENU_REPEAT_DELAY_MS 500U /* K1/K2 首次连发等待时间，单位：毫秒 */
#define APP_MENU_REPEAT_PERIOD_MS 100U /* K1/K2 连发周期，单位：毫秒 */

/* 参数注册表和实时数据注册表使用的稳定 ID */
typedef enum {
    APP_PARAMETER_GROUP_SPEED_PID = 0x0100, /* 共同速度 PID 参数分组 */
    APP_PARAMETER_SPEED_KP = 0x0101,        /* 共同速度环比例系数 */
    APP_PARAMETER_SPEED_KI = 0x0102,        /* 共同速度环积分系数 */
    APP_PARAMETER_SPEED_KD = 0x0103,        /* 共同速度环微分系数 */
    APP_PARAMETER_SPEED_OUTPUT = 0x0104,    /* 共同速度环输出限幅 */
    APP_PARAMETER_SPEED_INTEGRAL = 0x0105,  /* 共同速度环积分限幅 */
    APP_LIVE_ROLL = 0x0201,                 /* 横滚角实时项 */
    APP_LIVE_PITCH = 0x0202,                /* 俯仰角实时项 */
    APP_LIVE_YAW = 0x0203,                  /* 航向角实时项 */
    APP_LIVE_ENCODER_LEFT = 0x0204,         /* 左编码器周期增量实时项 */
    APP_LIVE_ENCODER_RIGHT = 0x0205,        /* 右编码器周期增量实时项 */
    APP_LIVE_ULTRASONIC = 0x0206,           /* 超声波距离实时项 */
    APP_LIVE_CAMERA_TARGET = 0x0207,        /* 相机目标存在状态实时项 */
    APP_LIVE_CAMERA_X = 0x0208,             /* 相机横向偏差实时项 */
    APP_LIVE_CAMERA_Y = 0x0209,             /* 相机纵向偏差实时项 */
} app_registry_id_t;

/* 共同速度 PID 描述符绑定的字段 */
typedef enum {
    APP_SPEED_PID_FIELD_KP = 0, /* 比例系数 */
    APP_SPEED_PID_FIELD_KI,     /* 积分系数 */
    APP_SPEED_PID_FIELD_KD,     /* 微分系数 */
    APP_SPEED_PID_FIELD_OUTPUT, /* 输出对称限幅 */
    APP_SPEED_PID_FIELD_INTEGRAL, /* 积分对称限幅 */
} app_speed_pid_field_t;

/* 实时数据注册项绑定的 BSP 快照字段 */
typedef enum {
    APP_LIVE_SOURCE_ROLL = 0, /* 姿态快照横滚角 */
    APP_LIVE_SOURCE_PITCH,    /* 姿态快照俯仰角 */
    APP_LIVE_SOURCE_YAW,      /* 姿态快照航向角 */
    APP_LIVE_SOURCE_ENCODER_LEFT,  /* 双轮反馈快照左增量 */
    APP_LIVE_SOURCE_ENCODER_RIGHT, /* 双轮反馈快照右增量 */
    APP_LIVE_SOURCE_ULTRASONIC,    /* 超声波有效距离 */
    APP_LIVE_SOURCE_CAMERA_TARGET, /* 相机快照目标存在标志 */
    APP_LIVE_SOURCE_CAMERA_X,      /* 相机快照横向偏差 */
    APP_LIVE_SOURCE_CAMERA_Y,      /* 相机快照纵向偏差 */
} app_live_source_t;

/* 参数字段回调的静态上下文 */
typedef struct {
    app_speed_pid_field_t field; /* 映射到 pid_param_t 的目标字段 */
} app_speed_pid_context_t;

/* 实时数据回调的静态上下文 */
typedef struct {
    app_live_source_t source; /* 映射到一个 BSP 快照字段 */
} app_live_context_t;

static speed_controller_t speed_controller;
static parameter_service_t parameter_service;
static menu_service_t menu_service;
static status_code_t last_menu_status;

static app_speed_pid_context_t speed_kp_context = {.field = APP_SPEED_PID_FIELD_KP};
static app_speed_pid_context_t speed_ki_context = {.field = APP_SPEED_PID_FIELD_KI};
static app_speed_pid_context_t speed_kd_context = {.field = APP_SPEED_PID_FIELD_KD};
static app_speed_pid_context_t speed_output_context = {.field = APP_SPEED_PID_FIELD_OUTPUT};
static app_speed_pid_context_t speed_integral_context = {
    .field = APP_SPEED_PID_FIELD_INTEGRAL,
};

static app_live_context_t live_roll_context = {.source = APP_LIVE_SOURCE_ROLL};
static app_live_context_t live_pitch_context = {.source = APP_LIVE_SOURCE_PITCH};
static app_live_context_t live_yaw_context = {.source = APP_LIVE_SOURCE_YAW};
static app_live_context_t live_encoder_left_context = {
    .source = APP_LIVE_SOURCE_ENCODER_LEFT,
};
static app_live_context_t live_encoder_right_context = {
    .source = APP_LIVE_SOURCE_ENCODER_RIGHT,
};
static app_live_context_t live_ultrasonic_context = {
    .source = APP_LIVE_SOURCE_ULTRASONIC,
};
static app_live_context_t live_camera_target_context = {
    .source = APP_LIVE_SOURCE_CAMERA_TARGET,
};
static app_live_context_t live_camera_x_context = {.source = APP_LIVE_SOURCE_CAMERA_X};
static app_live_context_t live_camera_y_context = {.source = APP_LIVE_SOURCE_CAMERA_Y};

/**
 * @brief 从唯一速度控制器读取一个共同 PID 字段
 * @param context 指向静态 app_speed_pid_context_t 的回调上下文
 * @param value 接收浮点参数值的存储地址
 * @retval STATUS_OK 参数值已写入
 * @retval STATUS_INVALID_ARGUMENT 上下文、输出或字段非法
 * @retval STATUS_NOT_INITIALIZED 速度控制器尚未初始化
 * @retval STATUS_STATE_ERROR 左右速度 PID 当前不一致
 */
static status_code_t app_speed_pid_read(void *context, scalar_value_t *value)
{
    const app_speed_pid_context_t *binding = (const app_speed_pid_context_t *)context;
    pid_param_t parameters;
    status_code_t status;

    if (!binding || !value) {
        return STATUS_INVALID_ARGUMENT;
    }
    status = speed_controller_get_common_speed_pid(&speed_controller, &parameters);
    if (status != STATUS_OK) {
        return status;
    }
    value->type = SCALAR_VALUE_FLOAT;
    switch (binding->field) {
        case APP_SPEED_PID_FIELD_KP:
            value->data.float_value = parameters.kp;
            break;
        case APP_SPEED_PID_FIELD_KI:
            value->data.float_value = parameters.ki;
            break;
        case APP_SPEED_PID_FIELD_KD:
            value->data.float_value = parameters.kd;
            break;
        case APP_SPEED_PID_FIELD_OUTPUT:
            value->data.float_value = parameters.out_max;
            break;
        case APP_SPEED_PID_FIELD_INTEGRAL:
            value->data.float_value = parameters.integral_max;
            break;
        default:
            return STATUS_INVALID_ARGUMENT;
    }
    return STATUS_OK;
}

/**
 * @brief 修改一个共同 PID 字段并整体同步左右速度环
 * @param context 指向静态 app_speed_pid_context_t 的回调上下文
 * @param value 待应用的浮点参数值
 * @retval STATUS_OK 左右速度环已同步且动态历史已清除
 * @retval STATUS_INVALID_ARGUMENT 上下文、类型、字段或 PID 数值非法
 * @retval STATUS_NOT_INITIALIZED 速度控制器尚未初始化
 * @retval STATUS_STATE_ERROR 左右速度 PID 不一致或整体应用失败
 */
static status_code_t app_speed_pid_write(void *context, const scalar_value_t *value)
{
    const app_speed_pid_context_t *binding = (const app_speed_pid_context_t *)context;
    pid_param_t parameters;
    status_code_t status;

    if (!binding || !value || (value->type != SCALAR_VALUE_FLOAT)) {
        return STATUS_INVALID_ARGUMENT;
    }
    status = speed_controller_get_common_speed_pid(&speed_controller, &parameters);
    if (status != STATUS_OK) {
        return status;
    }
    switch (binding->field) {
        case APP_SPEED_PID_FIELD_KP:
            parameters.kp = value->data.float_value;
            break;
        case APP_SPEED_PID_FIELD_KI:
            parameters.ki = value->data.float_value;
            break;
        case APP_SPEED_PID_FIELD_KD:
            parameters.kd = value->data.float_value;
            break;
        case APP_SPEED_PID_FIELD_OUTPUT:
            parameters.out_max = value->data.float_value;
            break;
        case APP_SPEED_PID_FIELD_INTEGRAL:
            parameters.integral_max = value->data.float_value;
            break;
        default:
            return STATUS_INVALID_ARGUMENT;
    }
    return speed_controller_apply_common_speed_pid(&speed_controller, &parameters);
}

static const parameter_service_descriptor_t speed_pid_parameters[] = {
    {
        .id = APP_PARAMETER_SPEED_KP,
        .name = "Kp",
        .unit = "",
        .type = SCALAR_VALUE_FLOAT,
        .minimum = {.type = SCALAR_VALUE_FLOAT, .data.float_value = 0.0F},
        .maximum = {.type = SCALAR_VALUE_FLOAT, .data.float_value = 100.0F},
        .step = {.type = SCALAR_VALUE_FLOAT, .data.float_value = 0.1F},
        .decimals = 1U,
        .is_writable = true,
        .read = app_speed_pid_read,
        .write = app_speed_pid_write,
        .context = &speed_kp_context,
    },
    {
        .id = APP_PARAMETER_SPEED_KI,
        .name = "Ki",
        .unit = "",
        .type = SCALAR_VALUE_FLOAT,
        .minimum = {.type = SCALAR_VALUE_FLOAT, .data.float_value = 0.0F},
        .maximum = {.type = SCALAR_VALUE_FLOAT, .data.float_value = 20.0F},
        .step = {.type = SCALAR_VALUE_FLOAT, .data.float_value = 0.1F},
        .decimals = 1U,
        .is_writable = true,
        .read = app_speed_pid_read,
        .write = app_speed_pid_write,
        .context = &speed_ki_context,
    },
    {
        .id = APP_PARAMETER_SPEED_KD,
        .name = "Kd",
        .unit = "",
        .type = SCALAR_VALUE_FLOAT,
        .minimum = {.type = SCALAR_VALUE_FLOAT, .data.float_value = 0.0F},
        .maximum = {.type = SCALAR_VALUE_FLOAT, .data.float_value = 20.0F},
        .step = {.type = SCALAR_VALUE_FLOAT, .data.float_value = 0.1F},
        .decimals = 1U,
        .is_writable = true,
        .read = app_speed_pid_read,
        .write = app_speed_pid_write,
        .context = &speed_kd_context,
    },
    {
        .id = APP_PARAMETER_SPEED_OUTPUT,
        .name = "Output",
        .unit = "0.1%",
        .type = SCALAR_VALUE_FLOAT,
        .minimum = {.type = SCALAR_VALUE_FLOAT, .data.float_value = 1.0F},
        .maximum = {.type = SCALAR_VALUE_FLOAT, .data.float_value = 1000.0F},
        .step = {.type = SCALAR_VALUE_FLOAT, .data.float_value = 50.0F},
        .decimals = 0U,
        .is_writable = true,
        .read = app_speed_pid_read,
        .write = app_speed_pid_write,
        .context = &speed_output_context,
    },
    {
        .id = APP_PARAMETER_SPEED_INTEGRAL,
        .name = "Integral",
        .unit = "",
        .type = SCALAR_VALUE_FLOAT,
        .minimum = {.type = SCALAR_VALUE_FLOAT, .data.float_value = 0.0F},
        .maximum = {.type = SCALAR_VALUE_FLOAT, .data.float_value = 10000.0F},
        .step = {.type = SCALAR_VALUE_FLOAT, .data.float_value = 100.0F},
        .decimals = 0U,
        .is_writable = true,
        .read = app_speed_pid_read,
        .write = app_speed_pid_write,
        .context = &speed_integral_context,
    },
};

static const parameter_service_group_t parameter_groups[] = {
    {
        .id = APP_PARAMETER_GROUP_SPEED_PID,
        .name = "Speed PID",
        .parameters = speed_pid_parameters,
        .parameter_count = sizeof(speed_pid_parameters) / sizeof(speed_pid_parameters[0]),
    },
};

_Static_assert(sizeof(speed_pid_parameters) / sizeof(speed_pid_parameters[0]) == 5U,
    "speed PID registry must contain five fields");
_Static_assert(sizeof(speed_pid_parameters) / sizeof(speed_pid_parameters[0]) <=
        MENU_SERVICE_STEP_CACHE_CAPACITY,
    "menu step cache must hold all speed PID fields");

/**
 * @brief 读取一项 BSP 运行快照并转换为强类型实时值
 * @param context 指向静态 app_live_context_t 的回调上下文
 * @param value 接收实时值的存储地址
 * @retval STATUS_OK 有效快照值已写入
 * @retval STATUS_INVALID_ARGUMENT 上下文、输出或来源非法
 * @retval STATUS_NOT_INITIALIZED 对应板级能力尚未初始化
 * @retval STATUS_UNAVAILABLE 对应快照当前无有效数据
 */
static status_code_t app_live_read(void *context, scalar_value_t *value)
{
    const app_live_context_t *binding = (const app_live_context_t *)context;
    bsp_gyro_snapshot_t gyro;
    bsp_feedback_snapshot_t feedback;
    bsp_camera_snapshot_t camera;
    uint16_t distance_mm;
    bool is_valid;
    status_code_t status;

    if (!binding || !value) {
        return STATUS_INVALID_ARGUMENT;
    }
    switch (binding->source) {
        case APP_LIVE_SOURCE_ROLL:
        case APP_LIVE_SOURCE_PITCH:
        case APP_LIVE_SOURCE_YAW:
            status = bsp_gyro_snapshot(&gyro);
            if ((status != STATUS_OK) || !gyro.is_valid) {
                return status == STATUS_OK ? STATUS_UNAVAILABLE : status;
            }
            value->type = SCALAR_VALUE_FLOAT;
            if (binding->source == APP_LIVE_SOURCE_ROLL) {
                value->data.float_value = gyro.roll;
            } else if (binding->source == APP_LIVE_SOURCE_PITCH) {
                value->data.float_value = gyro.pitch;
            } else {
                value->data.float_value = gyro.yaw;
            }
            return STATUS_OK;
        case APP_LIVE_SOURCE_ENCODER_LEFT:
        case APP_LIVE_SOURCE_ENCODER_RIGHT:
            status = bsp_feedback_get(&feedback);
            if (status != STATUS_OK) {
                return status;
            }
            value->type = SCALAR_VALUE_INT32;
            value->data.int32_value = binding->source == APP_LIVE_SOURCE_ENCODER_LEFT
                                          ? feedback.left_delta
                                          : feedback.right_delta;
            return STATUS_OK;
        case APP_LIVE_SOURCE_ULTRASONIC:
            status = bsp_ultrasonic_get(&distance_mm, &is_valid);
            if ((status != STATUS_OK) || !is_valid) {
                return status == STATUS_OK ? STATUS_UNAVAILABLE : status;
            }
            value->type = SCALAR_VALUE_UINT32;
            value->data.uint32_value = distance_mm;
            return STATUS_OK;
        case APP_LIVE_SOURCE_CAMERA_TARGET:
        case APP_LIVE_SOURCE_CAMERA_X:
        case APP_LIVE_SOURCE_CAMERA_Y:
            status = bsp_camera_snapshot(&camera);
            if ((status != STATUS_OK) || !camera.is_valid) {
                return status == STATUS_OK ? STATUS_UNAVAILABLE : status;
            }
            if (binding->source == APP_LIVE_SOURCE_CAMERA_TARGET) {
                value->type = SCALAR_VALUE_BOOL;
                value->data.bool_value = camera.has_target;
            } else {
                value->type = SCALAR_VALUE_INT32;
                value->data.int32_value = binding->source == APP_LIVE_SOURCE_CAMERA_X
                                              ? camera.error_x
                                              : camera.error_y;
            }
            return STATUS_OK;
        default:
            return STATUS_INVALID_ARGUMENT;
    }
}

static const menu_service_live_item_t live_items[] = {
    {
        .id = APP_LIVE_ROLL,
        .name = "Roll",
        .unit = "deg",
        .type = SCALAR_VALUE_FLOAT,
        .decimals = 2U,
        .read = app_live_read,
        .context = &live_roll_context,
    },
    {
        .id = APP_LIVE_PITCH,
        .name = "Pitch",
        .unit = "deg",
        .type = SCALAR_VALUE_FLOAT,
        .decimals = 2U,
        .read = app_live_read,
        .context = &live_pitch_context,
    },
    {
        .id = APP_LIVE_YAW,
        .name = "Yaw",
        .unit = "deg",
        .type = SCALAR_VALUE_FLOAT,
        .decimals = 2U,
        .read = app_live_read,
        .context = &live_yaw_context,
    },
    {
        .id = APP_LIVE_ENCODER_LEFT,
        .name = "EncL",
        .unit = "c/10ms",
        .type = SCALAR_VALUE_INT32,
        .decimals = 0U,
        .read = app_live_read,
        .context = &live_encoder_left_context,
    },
    {
        .id = APP_LIVE_ENCODER_RIGHT,
        .name = "EncR",
        .unit = "c/10ms",
        .type = SCALAR_VALUE_INT32,
        .decimals = 0U,
        .read = app_live_read,
        .context = &live_encoder_right_context,
    },
    {
        .id = APP_LIVE_ULTRASONIC,
        .name = "Ultra",
        .unit = "mm",
        .type = SCALAR_VALUE_UINT32,
        .decimals = 0U,
        .read = app_live_read,
        .context = &live_ultrasonic_context,
    },
    {
        .id = APP_LIVE_CAMERA_TARGET,
        .name = "Target",
        .unit = "",
        .type = SCALAR_VALUE_BOOL,
        .decimals = 0U,
        .read = app_live_read,
        .context = &live_camera_target_context,
    },
    {
        .id = APP_LIVE_CAMERA_X,
        .name = "CamX",
        .unit = "px",
        .type = SCALAR_VALUE_INT32,
        .decimals = 0U,
        .read = app_live_read,
        .context = &live_camera_x_context,
    },
    {
        .id = APP_LIVE_CAMERA_Y,
        .name = "CamY",
        .unit = "px",
        .type = SCALAR_VALUE_INT32,
        .decimals = 0U,
        .read = app_live_read,
        .context = &live_camera_y_context,
    },
};

/**
 * @brief 将 BSP 单调时间适配为菜单时间端口
 * @param context 当前适配器不使用的上下文
 * @param now_ms 接收当前时间的存储地址
 * @retval STATUS_OK 当前时间已写入
 * @retval STATUS_INVALID_ARGUMENT now_ms 为空
 */
static status_code_t app_menu_get_time(void *context, uint32_t *now_ms)
{
    (void)context;
    if (!now_ms) {
        return STATUS_INVALID_ARGUMENT;
    }
    *now_ms = bsp_time_get_ms();
    return STATUS_OK;
}

/**
 * @brief 将 BSP 五键稳定状态适配为菜单输入端口
 * @param context 当前适配器不使用的上下文
 * @param state 接收稳定按下位掩码的存储地址
 * @retval STATUS_OK 按键状态已写入
 * @retval STATUS_INVALID_ARGUMENT state 为空
 * @retval STATUS_UNAVAILABLE 板级按键能力不可用
 */
static status_code_t app_menu_get_keys(void *context, uint8_t *state)
{
    uint8_t pressed_events;

    (void)context;
    if (!state) {
        return STATUS_INVALID_ARGUMENT;
    }
    return bsp_keys_get(state, &pressed_events);
}

/**
 * @brief 将 BSP OLED 帧就绪查询适配为菜单端口
 * @param context 当前适配器不使用的上下文
 * @param is_ready 接收显存可写状态的存储地址
 * @retval STATUS_OK 就绪状态已写入
 * @retval STATUS_INVALID_ARGUMENT is_ready 为空
 * @retval STATUS_UNAVAILABLE OLED 板级能力不可用
 * @retval STATUS_IO_ERROR OLED 已进入故障状态
 */
static status_code_t app_menu_frame_ready(void *context, bool *is_ready)
{
    (void)context;
    return bsp_oled_frame_ready(is_ready);
}

/** @brief 将 BSP OLED 清屏能力适配为菜单端口。 */
static status_code_t app_menu_clear(void *context)
{
    (void)context;
    return bsp_oled_clear();
}

/**
 * @brief 将 BSP OLED 像素写入能力适配为菜单端口
 * @param context 当前适配器不使用的上下文
 * @param x 像素横坐标
 * @param y 像素纵坐标
 * @param is_on 像素点亮标志
 * @retval STATUS_OK 像素已写入
 * @retval STATUS_OUT_OF_RANGE 坐标越界
 * @retval STATUS_UNAVAILABLE OLED 板级能力不可用
 */
static status_code_t app_menu_set_pixel(void *context, uint8_t x, uint8_t y, bool is_on)
{
    (void)context;
    return bsp_oled_set_pixel(x, y, is_on);
}

/** @brief 将 BSP OLED 非阻塞刷新请求适配为菜单端口。 */
static status_code_t app_menu_refresh(void *context)
{
    (void)context;
    return bsp_oled_refresh();
}

/**
 * @brief 初始化当前产品的板级能力、领域对象和上层服务
 * @retval STATUS_OK 应用组合完成
 * @retval STATUS_INVALID_ARGUMENT 静态注册表或领域默认配置无效
 * @retval STATUS_IO_ERROR 必需板级能力初始化失败
 * @retval STATUS_STATE_ERROR 参数或实时数据稳定 ID 冲突
 * @retval STATUS_OUT_OF_RANGE 注册项超过服务容量
 */
status_code_t app_init(void)
{
    menu_service_config_t menu_config;
    status_code_t status;

    status = bsp_board_init();
    if (status != STATUS_OK) {
        return status;
    }
    status = speed_controller_init_default(&speed_controller);
    if (status != STATUS_OK) {
        return status;
    }
    status = parameter_service_init(&parameter_service, parameter_groups,
        sizeof(parameter_groups) / sizeof(parameter_groups[0]));
    if (status != STATUS_OK) {
        return status;
    }
    menu_config = (menu_service_config_t){
        .parameters = &parameter_service,
        .live_items = live_items,
        .live_item_count = sizeof(live_items) / sizeof(live_items[0]),
        .port =
            {
                .get_time = app_menu_get_time,
                .get_keys = app_menu_get_keys,
                .frame_ready = app_menu_frame_ready,
                .clear = app_menu_clear,
                .set_pixel = app_menu_set_pixel,
                .refresh = app_menu_refresh,
                .context = NULL,
            },
        .debug_refresh_period_ms = APP_DEBUG_REFRESH_PERIOD_MS,
        .live_refresh_period_ms = APP_LIVE_REFRESH_PERIOD_MS,
        .long_press_ms = APP_MENU_LONG_PRESS_MS,
        .repeat_delay_ms = APP_MENU_REPEAT_DELAY_MS,
        .repeat_period_ms = APP_MENU_REPEAT_PERIOD_MS,
    };
    status = menu_service_init(&menu_service, &menu_config);
    if (status != STATUS_OK) {
        return status;
    }
    last_menu_status = STATUS_OK;
    return STATUS_OK;
}

/**
 * @brief 按依赖顺序推进一次 BSP 和 OLED 菜单协作式任务
 */
void app_run_once(void)
{
    status_code_t status;

    bsp_board_process();
    status = menu_service_process(&menu_service);
    if ((status != STATUS_OK) && (status != last_menu_status)) {
        error_service_record(STATUS_SOURCE_MENU, status, bsp_time_get_ms());
    }
    last_menu_status = status;
}
