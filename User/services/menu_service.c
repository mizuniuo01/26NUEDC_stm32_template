/**
 * @file menu_service.c
 * @brief 通过五键和 128×64 单色屏提供运行期调参与实时数据显示。
 * @note 本文件位于 Services，只编排参数服务、实时数据回调和抽象显示端口，不 include
 *       BSP、Driver、HAL 或任何具体外设句柄。
 * @note 使用者静态分配 menu_service_t，并准备生命周期覆盖服务的 Parameter Service、
 *       实时项数组、名称字符串和回调上下文。所有数组数量均由定义数组的翻译单元使用
 *       sizeof(array) / sizeof(array[0]) 推导，再填入 menu_service_config_t。
 * @note 显示适配只需注入单调时间、五键稳定状态、帧就绪查询、清屏、像素写入和非阻塞
 *       刷新六项能力。当前板卡的 BSP 适配集中位于 User/app/app.c；换板时不修改本文件。
 * @note 参数必须先注册到 Parameter Service。描述符明确声明稳定 ID、名称、单位、强类型、
 *       闭区间、默认步长、小数位和读写回调；参数所有者仍是提供回调的 Domain 或 Service。
 * @note 实时项通过 menu_service_live_item_t 注册。读取成功时回调必须返回与声明类型一致的
 *       scalar_value_t；数据尚未有效时返回 STATUS_UNAVAILABLE，界面固定显示“--”。
 * @note 显示模式默认 Debug。K5 按下沿在 Debug/Live 间切换，并保留调试页、实时滚动位置
 *       和未提交草稿。K5 与其他按键同周期触发时仅处理模式切换。
 * @note Debug 交互为 L1 分组、L2 参数、L3 数值和步长。K1/K2 导航或增减，K3 短按
 *       进入/确认、长按进入步长编辑，K4 返回/取消。数值在注册范围内钳制，列表首尾循环。
 * @note 草稿确认前不写入参数所有者。确认时重读当前值；若其他入口已修改同一参数，显示
 *       Conflict 并重载当前值，不覆盖外部新值。
 * @note 显示端口必须把 OLED 显存的独占所有权交给本菜单。frame_ready 返回 false 时不清屏、
 *       不写像素且不排队，从而避免上一帧异步发送期间显存被改写。
 * @note 接入顺序为：BSP 和参数所有者 → Parameter Service → menu_service_init()。主循环先
 *       调用 bsp_board_process()，再调用 menu_service_process()。
 * @note menu_service_process() 不使用阻塞延时；按键长按、连发、通知和刷新周期全部由注入的
 *       可回绕 uint32_t 毫秒时钟推进。Debug/Live 刷新周期和按键时序均由配置显式给出。
 * @note 当前 AutoBallCar 适配的按键、OLED、参数编辑和实时数据已完成上板验证，验证范围与
 *       PCB/CubeMX 按键映射记录在 docs/MENU_SERVICE_VALIDATION.md。
 * @warning 本服务只能在协作式主循环或任务上下文调用，不允许从 ISR 调用，也不阻塞等待。
 * @warning 服务会直接格式化并绘制注册名称。名称、单位和格式化结果必须适合 128×64 布局；
 *          当前实现只支持 oled_data 中的可打印 ASCII 字模，不支持中文或动态分配字符串。
 */
#include "menu_service.h"
#include "oled_data.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MENU_SERVICE_DISPLAY_WIDTH_PIXELS 128U /* 菜单布局所需像素宽度 */
#define MENU_SERVICE_DISPLAY_HEIGHT_PIXELS 64U /* 菜单布局所需像素高度 */
#define MENU_SERVICE_SMALL_FONT_WIDTH_PIXELS 6U /* 6×8 字模字符宽度 */
#define MENU_SERVICE_SMALL_FONT_HEIGHT_PIXELS 8U /* 6×8 字模字符高度 */
#define MENU_SERVICE_LARGE_FONT_WIDTH_PIXELS 8U /* 8×16 字模字符宽度 */
#define MENU_SERVICE_LARGE_FONT_HEIGHT_PIXELS 16U /* 8×16 字模字符高度 */
#define MENU_SERVICE_TEXT_COLUMN_CAPACITY 21U /* 一行 6×8 字符的完整可见列数 */
#define MENU_SERVICE_VISIBLE_ITEM_COUNT 6U /* 标题和提示之间的可见数据行数 */
#define MENU_SERVICE_HINT_ROW 7U /* 底部提示或通知所在字符行 */
#define MENU_SERVICE_LINE_CAPACITY 48U /* 格式化单行文本的字节容量 */
#define MENU_SERVICE_VALUE_CAPACITY 24U /* 格式化标量值的字节容量 */
#define MENU_SERVICE_NOTIFICATION_PERIOD_MS 1000U /* 短通知保持时间，单位：毫秒 */
#define MENU_SERVICE_KEY_UP 0U /* K1：上翻或增大 */
#define MENU_SERVICE_KEY_DOWN 1U /* K2：下翻或减小 */
#define MENU_SERVICE_KEY_CONFIRM 2U /* K3：进入、确认和长按步长编辑 */
#define MENU_SERVICE_KEY_CANCEL 3U /* K4：返回或取消 */
#define MENU_SERVICE_KEY_MODE 4U /* K5：切换 Debug/Live 模式 */
#define MENU_SERVICE_CONTROL_KEY_MASK 0x0FU /* K1–K4 控制键位掩码 */

/* 一次主循环最多交付给状态机的一个抽象动作 */
typedef enum {
    MENU_SERVICE_ACTION_NONE = 0, /* 本周期无可消费动作 */
    MENU_SERVICE_ACTION_CONFIRM,  /* K3 短按释放 */
    MENU_SERVICE_ACTION_CONFIRM_LONG, /* K3 达到长按阈值 */
    MENU_SERVICE_ACTION_CANCEL,       /* K4 释放 */
    MENU_SERVICE_ACTION_UP,           /* K1 按下或连发 */
    MENU_SERVICE_ACTION_DOWN,         /* K2 按下或连发 */
    MENU_SERVICE_ACTION_MODE,         /* K5 按下 */
} menu_service_action_t;

/**
 * @brief 判断当前时间是否已到达可回绕毫秒截止点
 * @param now_ms 当前单调毫秒时间
 * @param deadline_ms 待判断截止时间
 * @retval true 截止点已到达
 * @retval false 截止点尚未到达
 */
static bool menu_service_deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

/**
 * @brief 检查标量类型是否可由菜单格式化
 * @param type 待检查标量类型
 * @retval true 类型受支持
 * @retval false 类型不受支持
 */
static bool menu_service_type_is_valid(scalar_value_type_t type)
{
    return (type == SCALAR_VALUE_FLOAT) || (type == SCALAR_VALUE_INT32) ||
           (type == SCALAR_VALUE_UINT32) || (type == SCALAR_VALUE_BOOL);
}

/**
 * @brief 判断两个强类型标量是否完全相等
 * @param left 左操作数
 * @param right 右操作数
 * @retval true 类型和值都相等
 * @retval false 类型或值不同
 */
static bool menu_service_value_is_equal(const scalar_value_t *left,
    const scalar_value_t *right)
{
    if (!left || !right || (left->type != right->type)) {
        return false;
    }
    switch (left->type) {
        case SCALAR_VALUE_FLOAT:
            return left->data.float_value == right->data.float_value;
        case SCALAR_VALUE_INT32:
            return left->data.int32_value == right->data.int32_value;
        case SCALAR_VALUE_UINT32:
            return left->data.uint32_value == right->data.uint32_value;
        case SCALAR_VALUE_BOOL:
            return left->data.bool_value == right->data.bool_value;
        default:
            return false;
    }
}

/**
 * @brief 将强类型标量格式化为有界文本
 * @param value 待格式化标量
 * @param decimals 浮点显示小数位数
 * @param buffer 接收文本的缓冲区
 * @param capacity buffer 容量，单位：字节
 * @retval STATUS_OK 文本已完整写入
 * @retval STATUS_INVALID_ARGUMENT 参数为空、容量为零、浮点非有限或类型非法
 * @retval STATUS_OUT_OF_RANGE 格式化文本被截断
 */
static status_code_t menu_service_format_value(const scalar_value_t *value, uint8_t decimals,
    char *buffer, size_t capacity)
{
    int result;

    if (!value || !buffer || (capacity == 0U)) {
        return STATUS_INVALID_ARGUMENT;
    }
    switch (value->type) {
        case SCALAR_VALUE_FLOAT:
            if (!isfinite(value->data.float_value)) {
                return STATUS_INVALID_ARGUMENT;
            }
            result = snprintf(buffer, capacity, "%.*f", (int)decimals,
                (double)value->data.float_value);
            break;
        case SCALAR_VALUE_INT32:
            result = snprintf(buffer, capacity, "%" PRId32, value->data.int32_value);
            break;
        case SCALAR_VALUE_UINT32:
            result = snprintf(buffer, capacity, "%" PRIu32, value->data.uint32_value);
            break;
        case SCALAR_VALUE_BOOL:
            result = snprintf(buffer, capacity, "%s", value->data.bool_value ? "ON" : "OFF");
            break;
        default:
            return STATUS_INVALID_ARGUMENT;
    }
    if (result < 0) {
        return STATUS_INVALID_ARGUMENT;
    }
    return (size_t)result < capacity ? STATUS_OK : STATUS_OUT_OF_RANGE;
}

/**
 * @brief 对标量草稿执行一次有边界增减
 * @param descriptor 当前参数元数据
 * @param step 当前运行期步长
 * @param direction 正数增大、负数减小
 * @param value 待就地调整的草稿值
 */
static void menu_service_adjust_draft(const parameter_service_descriptor_t *descriptor,
    const scalar_value_t *step, int32_t direction, scalar_value_t *value)
{
    double adjusted_float;
    int64_t adjusted;

    switch (descriptor->type) {
        case SCALAR_VALUE_FLOAT:
            adjusted_float = (double)value->data.float_value +
                             (double)step->data.float_value * (double)direction;
            if (adjusted_float < (double)descriptor->minimum.data.float_value) {
                *value = descriptor->minimum;
            } else if (adjusted_float > (double)descriptor->maximum.data.float_value) {
                *value = descriptor->maximum;
            } else {
                value->data.float_value = (float)adjusted_float;
            }
            break;
        case SCALAR_VALUE_INT32:
            adjusted = (int64_t)value->data.int32_value +
                       (int64_t)step->data.int32_value * (int64_t)direction;
            if (adjusted < (int64_t)descriptor->minimum.data.int32_value) {
                *value = descriptor->minimum;
            } else if (adjusted > (int64_t)descriptor->maximum.data.int32_value) {
                *value = descriptor->maximum;
            } else {
                value->data.int32_value = (int32_t)adjusted;
            }
            break;
        case SCALAR_VALUE_UINT32:
            adjusted = (int64_t)value->data.uint32_value +
                       (int64_t)step->data.uint32_value * (int64_t)direction;
            if (adjusted < (int64_t)descriptor->minimum.data.uint32_value) {
                *value = descriptor->minimum;
            } else if (adjusted > (int64_t)descriptor->maximum.data.uint32_value) {
                *value = descriptor->maximum;
            } else {
                value->data.uint32_value = (uint32_t)adjusted;
            }
            break;
        case SCALAR_VALUE_BOOL:
            value->data.bool_value = direction > 0;
            break;
        default:
            break;
    }
}

/**
 * @brief 将当前步长扩大或缩小十倍
 * @param direction 正数扩大、负数缩小
 * @param step 待就地修改的步长
 */
static void menu_service_scale_step(int32_t direction, scalar_value_t *step)
{
    float scaled_float;

    switch (step->type) {
        case SCALAR_VALUE_FLOAT:
            scaled_float = direction > 0 ? step->data.float_value * 10.0F
                                         : step->data.float_value / 10.0F;
            if (isfinite(scaled_float) && (scaled_float > 0.0F)) {
                step->data.float_value = scaled_float;
            }
            break;
        case SCALAR_VALUE_INT32:
            if ((direction > 0) && (step->data.int32_value <= INT32_MAX / 10)) {
                step->data.int32_value *= 10;
            } else if ((direction < 0) && (step->data.int32_value >= 10)) {
                step->data.int32_value /= 10;
            }
            break;
        case SCALAR_VALUE_UINT32:
            if ((direction > 0) && (step->data.uint32_value <= UINT32_MAX / 10U)) {
                step->data.uint32_value *= 10U;
            } else if ((direction < 0) && (step->data.uint32_value >= 10U)) {
                step->data.uint32_value /= 10U;
            }
            break;
        case SCALAR_VALUE_BOOL:
        default:
            break;
    }
}

/**
 * @brief 计算组内参数对应的线性步长缓存索引
 * @param service 已初始化参数服务
 * @param group_index 分组索引
 * @param parameter_index 组内参数索引
 * @return 从零开始的参数线性索引
 */
static size_t menu_service_linear_index(const parameter_service_t *service, size_t group_index,
    size_t parameter_index)
{
    size_t index = parameter_index;
    size_t i;

    for (i = 0U; i < group_index; i++) {
        index += service->groups[i].parameter_count;
    }
    return index;
}

/**
 * @brief 调整列表滚动窗口以保证选中项可见
 * @param index 当前选中索引
 * @param count 列表元素总数
 * @param scroll 待就地修改的窗口起点
 */
static void menu_service_adjust_scroll(size_t index, size_t count, size_t *scroll)
{
    if (count <= MENU_SERVICE_VISIBLE_ITEM_COUNT) {
        *scroll = 0U;
    } else if (index < *scroll) {
        *scroll = index;
    } else if (index >= (*scroll + MENU_SERVICE_VISIBLE_ITEM_COUNT)) {
        *scroll = index - MENU_SERVICE_VISIBLE_ITEM_COUNT + 1U;
    }
}

/**
 * @brief 为当前帧设置一个点亮像素
 * @param menu 已绑定显示端口的菜单实例
 * @param x 像素横坐标
 * @param y 像素纵坐标
 * @retval STATUS_OK 像素已写入
 * @retval STATUS_IO_ERROR 显示端口拒绝像素写入
 */
static status_code_t menu_service_draw_pixel(menu_service_t *menu, uint8_t x, uint8_t y)
{
    return menu->config.port.set_pixel(menu->config.port.context, x, y, true);
}

/**
 * @brief 绘制一个 6×8 ASCII 字符
 * @param menu 菜单实例
 * @param row 从零开始的字符行
 * @param column 从零开始的字符列
 * @param character 可打印 ASCII，其他字符按问号绘制
 * @retval STATUS_OK 字符已绘制
 * @retval STATUS_OUT_OF_RANGE 行或列超出显示范围
 * @retval STATUS_IO_ERROR 显示端口拒绝像素写入
 */
static status_code_t menu_service_draw_small_char(menu_service_t *menu, uint8_t row,
    uint8_t column, char character)
{
    const uint8_t *glyph;
    uint8_t glyph_column;
    uint8_t glyph_row;
    status_code_t status;

    if ((row >= MENU_SERVICE_DISPLAY_HEIGHT_PIXELS / MENU_SERVICE_SMALL_FONT_HEIGHT_PIXELS) ||
        (column >= MENU_SERVICE_TEXT_COLUMN_CAPACITY)) {
        return STATUS_OUT_OF_RANGE;
    }
    if ((character < ' ') || (character > '~')) {
        character = '?';
    }
    glyph = oled_font_6x8[(uint8_t)character - (uint8_t)' '];
    for (glyph_column = 0U; glyph_column < MENU_SERVICE_SMALL_FONT_WIDTH_PIXELS;
         glyph_column++) {
        for (glyph_row = 0U; glyph_row < MENU_SERVICE_SMALL_FONT_HEIGHT_PIXELS;
             glyph_row++) {
            uint8_t x;
            uint8_t y;

            if ((glyph[glyph_column] & (uint8_t)(1U << glyph_row)) == 0U) {
                continue;
            }
            x = (uint8_t)(column * MENU_SERVICE_SMALL_FONT_WIDTH_PIXELS + glyph_column);
            y = (uint8_t)(row * MENU_SERVICE_SMALL_FONT_HEIGHT_PIXELS + glyph_row);
            status = menu_service_draw_pixel(menu, x, y);
            if (status != STATUS_OK) {
                return status;
            }
        }
    }
    return STATUS_OK;
}

/**
 * @brief 绘制一行有界 6×8 ASCII 文本
 * @param menu 菜单实例
 * @param row 从零开始的字符行
 * @param column 起始字符列
 * @param text 以空字符结尾的文本，超出屏幕部分会截断
 * @retval STATUS_OK 所有可见字符已绘制
 * @retval STATUS_INVALID_ARGUMENT text 为空
 * @retval STATUS_OUT_OF_RANGE 起始行或列越界
 * @retval STATUS_IO_ERROR 显示端口拒绝像素写入
 */
static status_code_t menu_service_draw_small_text(menu_service_t *menu, uint8_t row,
    uint8_t column, const char *text)
{
    size_t index;
    status_code_t status;

    if (!text) {
        return STATUS_INVALID_ARGUMENT;
    }
    if ((row >= MENU_SERVICE_DISPLAY_HEIGHT_PIXELS / MENU_SERVICE_SMALL_FONT_HEIGHT_PIXELS) ||
        (column >= MENU_SERVICE_TEXT_COLUMN_CAPACITY)) {
        return STATUS_OUT_OF_RANGE;
    }
    for (index = 0U; (text[index] != '\0') &&
                     ((size_t)column + index < MENU_SERVICE_TEXT_COLUMN_CAPACITY);
         index++) {
        status = menu_service_draw_small_char(menu, row, (uint8_t)((size_t)column + index),
            text[index]);
        if (status != STATUS_OK) {
            return status;
        }
    }
    return STATUS_OK;
}

/**
 * @brief 绘制一个 8×16 ASCII 字符
 * @param menu 菜单实例
 * @param x 像素横坐标
 * @param y 像素纵坐标
 * @param character 可打印 ASCII，其他字符按问号绘制
 * @retval STATUS_OK 字符已绘制
 * @retval STATUS_OUT_OF_RANGE 字符超出显示边界
 * @retval STATUS_IO_ERROR 显示端口拒绝像素写入
 */
static status_code_t menu_service_draw_large_char(menu_service_t *menu, uint8_t x, uint8_t y,
    char character)
{
    const uint8_t *glyph;
    uint8_t glyph_column;
    uint8_t glyph_row;
    status_code_t status;

    if (((uint16_t)x + MENU_SERVICE_LARGE_FONT_WIDTH_PIXELS >
            MENU_SERVICE_DISPLAY_WIDTH_PIXELS) ||
        ((uint16_t)y + MENU_SERVICE_LARGE_FONT_HEIGHT_PIXELS >
            MENU_SERVICE_DISPLAY_HEIGHT_PIXELS)) {
        return STATUS_OUT_OF_RANGE;
    }
    if ((character < ' ') || (character > '~')) {
        character = '?';
    }
    glyph = oled_font_8x16[(uint8_t)character - (uint8_t)' '];
    for (glyph_column = 0U; glyph_column < MENU_SERVICE_LARGE_FONT_WIDTH_PIXELS;
         glyph_column++) {
        for (glyph_row = 0U; glyph_row < MENU_SERVICE_LARGE_FONT_HEIGHT_PIXELS;
             glyph_row++) {
            uint8_t byte_index =
                glyph_row < 8U ? glyph_column : (uint8_t)(glyph_column + 8U);
            uint8_t bit_index = (uint8_t)(glyph_row % 8U);

            if ((glyph[byte_index] & (uint8_t)(1U << bit_index)) == 0U) {
                continue;
            }
            status = menu_service_draw_pixel(menu, (uint8_t)(x + glyph_column),
                (uint8_t)(y + glyph_row));
            if (status != STATUS_OK) {
                return status;
            }
        }
    }
    return STATUS_OK;
}

/**
 * @brief 居中绘制一行有界 8×16 ASCII 文本
 * @param menu 菜单实例
 * @param y 文本顶部像素纵坐标
 * @param text 以空字符结尾的文本
 * @retval STATUS_OK 可见文本已绘制
 * @retval STATUS_INVALID_ARGUMENT text 为空
 * @retval STATUS_IO_ERROR 显示端口拒绝像素写入
 */
static status_code_t menu_service_draw_large_centered(menu_service_t *menu, uint8_t y,
    const char *text)
{
    size_t length;
    size_t index;
    uint8_t x;
    status_code_t status;

    if (!text) {
        return STATUS_INVALID_ARGUMENT;
    }
    length = strlen(text);
    if (length > MENU_SERVICE_DISPLAY_WIDTH_PIXELS / MENU_SERVICE_LARGE_FONT_WIDTH_PIXELS) {
        length = MENU_SERVICE_DISPLAY_WIDTH_PIXELS / MENU_SERVICE_LARGE_FONT_WIDTH_PIXELS;
    }
    x = (uint8_t)((MENU_SERVICE_DISPLAY_WIDTH_PIXELS -
                      length * MENU_SERVICE_LARGE_FONT_WIDTH_PIXELS) /
                  2U);
    for (index = 0U; index < length; index++) {
        status = menu_service_draw_large_char(menu,
            (uint8_t)(x + index * MENU_SERVICE_LARGE_FONT_WIDTH_PIXELS), y, text[index]);
        if (status != STATUS_OK) {
            return status;
        }
    }
    return STATUS_OK;
}

/**
 * @brief 设置一条自动失效的底部通知
 * @param menu 菜单实例
 * @param now_ms 当前单调毫秒时间
 * @param message 以空字符结尾的短消息
 */
static void menu_service_notify(menu_service_t *menu, uint32_t now_ms, const char *message)
{
    (void)snprintf(menu->notification, sizeof(menu->notification), "%s", message);
    menu->notification_until_ms = now_ms + MENU_SERVICE_NOTIFICATION_PERIOD_MS;
    menu->is_dirty = true;
}

/**
 * @brief 绘制当前通知或页面默认按键提示
 * @param menu 菜单实例
 * @param now_ms 当前单调毫秒时间
 * @param fallback 无有效通知时显示的提示
 * @retval STATUS_OK 底部文本已绘制
 * @retval STATUS_IO_ERROR 显示端口拒绝像素写入
 */
static status_code_t menu_service_draw_hint(menu_service_t *menu, uint32_t now_ms,
    const char *fallback)
{
    const char *text = fallback;

    if ((menu->notification[0] != '\0') &&
        !menu_service_deadline_reached(now_ms, menu->notification_until_ms)) {
        text = menu->notification;
    }
    return menu_service_draw_small_text(menu, MENU_SERVICE_HINT_ROW, 0U, text);
}

/**
 * @brief 绘制 Debug L1 参数分组列表
 * @param menu 菜单实例
 * @param now_ms 当前单调毫秒时间
 * @retval STATUS_OK 页面已绘制
 * @retval STATUS_IO_ERROR 显示端口拒绝像素写入
 */
static status_code_t menu_service_render_group(menu_service_t *menu, uint32_t now_ms)
{
    const parameter_service_group_t *group;
    char line[MENU_SERVICE_LINE_CAPACITY];
    size_t row;
    size_t index;
    status_code_t status;

    status = menu_service_draw_small_text(menu, 0U, 4U, "=== DEBUG ===");
    if (status != STATUS_OK) {
        return status;
    }
    for (row = 0U; row < MENU_SERVICE_VISIBLE_ITEM_COUNT; row++) {
        index = menu->group_scroll + row;
        if (index >= menu->config.parameters->group_count) {
            break;
        }
        status = parameter_service_get_group(menu->config.parameters, index, &group);
        if (status != STATUS_OK) {
            return status;
        }
        (void)snprintf(line, sizeof(line), "%c%s", index == menu->group_index ? '>' : ' ',
            group->name);
        status = menu_service_draw_small_text(menu, (uint8_t)(row + 1U), 0U, line);
        if (status != STATUS_OK) {
            return status;
        }
    }
    return menu_service_draw_hint(menu, now_ms, "1/2:nav 3:ent 5:live");
}

/**
 * @brief 绘制 Debug L2 参数列表及所有者当前值
 * @param menu 菜单实例
 * @param now_ms 当前单调毫秒时间
 * @retval STATUS_OK 页面已绘制
 * @retval STATUS_IO_ERROR 显示端口拒绝像素写入
 */
static status_code_t menu_service_render_parameter(menu_service_t *menu, uint32_t now_ms)
{
    const parameter_service_group_t *group;
    const parameter_service_descriptor_t *descriptor;
    scalar_value_t value;
    char value_text[MENU_SERVICE_VALUE_CAPACITY];
    char line[MENU_SERVICE_LINE_CAPACITY];
    size_t row;
    size_t index;
    status_code_t status;

    status = parameter_service_get_group(menu->config.parameters, menu->group_index, &group);
    if (status != STATUS_OK) {
        return status;
    }
    status = menu_service_draw_small_text(menu, 0U, 0U, group->name);
    if (status != STATUS_OK) {
        return status;
    }
    for (row = 0U; row < MENU_SERVICE_VISIBLE_ITEM_COUNT; row++) {
        index = menu->parameter_scroll + row;
        if (index >= group->parameter_count) {
            break;
        }
        descriptor = &group->parameters[index];
        status = parameter_service_get(menu->config.parameters, menu->group_index, index,
            &value);
        if ((status != STATUS_OK) ||
            (menu_service_format_value(&value, descriptor->decimals, value_text,
                 sizeof(value_text)) != STATUS_OK)) {
            (void)snprintf(value_text, sizeof(value_text), "--");
        }
        (void)snprintf(line, sizeof(line), "%c%s:%s",
            index == menu->parameter_index ? '>' : ' ', descriptor->name, value_text);
        status = menu_service_draw_small_text(menu, (uint8_t)(row + 1U), 0U, line);
        if (status != STATUS_OK) {
            return status;
        }
    }
    return menu_service_draw_hint(menu, now_ms, "1/2:nav 3:ent 4:back");
}

/**
 * @brief 绘制 Debug L3 数值或步长编辑页
 * @param menu 菜单实例
 * @param now_ms 当前单调毫秒时间
 * @retval STATUS_OK 页面已绘制
 * @retval STATUS_IO_ERROR 显示端口拒绝像素写入
 */
static status_code_t menu_service_render_edit(menu_service_t *menu, uint32_t now_ms)
{
    const parameter_service_group_t *group;
    const parameter_service_descriptor_t *descriptor;
    char value_text[MENU_SERVICE_VALUE_CAPACITY];
    char step_text[MENU_SERVICE_VALUE_CAPACITY];
    char minimum_text[MENU_SERVICE_VALUE_CAPACITY];
    char maximum_text[MENU_SERVICE_VALUE_CAPACITY];
    char line[MENU_SERVICE_LINE_CAPACITY];
    status_code_t status;

    status = parameter_service_get_group(menu->config.parameters, menu->group_index, &group);
    if (status != STATUS_OK) {
        return status;
    }
    descriptor = &group->parameters[menu->parameter_index];
    (void)snprintf(line, sizeof(line), "%s>%s", group->name, descriptor->name);
    status = menu_service_draw_small_text(menu, 0U, 0U, line);
    if (status != STATUS_OK) {
        return status;
    }
    status = menu_service_format_value(&menu->edit_value, descriptor->decimals, value_text,
        sizeof(value_text));
    if (status != STATUS_OK) {
        return status;
    }
    status = menu_service_draw_large_centered(menu, 16U, value_text);
    if (status != STATUS_OK) {
        return status;
    }
    status = menu_service_format_value(&menu->step_cache[menu->step_cache_index],
        descriptor->decimals, step_text, sizeof(step_text));
    if (status != STATUS_OK) {
        return status;
    }
    (void)snprintf(line, sizeof(line), "%sStep:%s",
        menu->debug_state == MENU_SERVICE_DEBUG_STEP ? ">" : "", step_text);
    status = menu_service_draw_small_text(menu, 4U, 0U, line);
    if (status != STATUS_OK) {
        return status;
    }
    status = menu_service_format_value(&descriptor->minimum, descriptor->decimals,
        minimum_text, sizeof(minimum_text));
    if (status != STATUS_OK) {
        return status;
    }
    status = menu_service_format_value(&descriptor->maximum, descriptor->decimals,
        maximum_text, sizeof(maximum_text));
    if (status != STATUS_OK) {
        return status;
    }
    (void)snprintf(line, sizeof(line), "%s~%s", minimum_text, maximum_text);
    status = menu_service_draw_small_text(menu, 5U, 0U, line);
    if (status != STATUS_OK) {
        return status;
    }
    return menu_service_draw_hint(menu, now_ms,
        menu->debug_state == MENU_SERVICE_DEBUG_STEP ? "1/2:stp 3:ok 4:cancel"
                                                      : "1/2:+/- 3:ok 4:cancel");
}

/**
 * @brief 绘制 Live 只读实时数据窗口
 * @param menu 菜单实例
 * @param now_ms 当前单调毫秒时间
 * @retval STATUS_OK 页面已绘制
 * @retval STATUS_IO_ERROR 显示端口拒绝像素写入
 */
static status_code_t menu_service_render_live(menu_service_t *menu, uint32_t now_ms)
{
    const menu_service_live_item_t *item;
    scalar_value_t value;
    char value_text[MENU_SERVICE_VALUE_CAPACITY];
    char line[MENU_SERVICE_LINE_CAPACITY];
    size_t row;
    size_t index;
    status_code_t status;

    status = menu_service_draw_small_text(menu, 0U, 5U, "=== LIVE ===");
    if (status != STATUS_OK) {
        return status;
    }
    for (row = 0U; (row < MENU_SERVICE_VISIBLE_ITEM_COUNT) &&
                   (row < menu->config.live_item_count);
         row++) {
        index = (menu->live_scroll + row) % menu->config.live_item_count;
        item = &menu->config.live_items[index];
        status = item->read(item->context, &value);
        if ((status != STATUS_OK) || (value.type != item->type) ||
            ((item->type == SCALAR_VALUE_FLOAT) && !isfinite(value.data.float_value)) ||
            (menu_service_format_value(&value, item->decimals, value_text,
                 sizeof(value_text)) != STATUS_OK)) {
            (void)snprintf(value_text, sizeof(value_text), "--");
        }
        if (item->unit[0] == '\0') {
            (void)snprintf(line, sizeof(line), "%s:%s", item->name, value_text);
        } else {
            (void)snprintf(line, sizeof(line), "%s:%s %s", item->name, value_text,
                item->unit);
        }
        status = menu_service_draw_small_text(menu, (uint8_t)(row + 1U), 0U, line);
        if (status != STATUS_OK) {
            return status;
        }
    }
    return menu_service_draw_hint(menu, now_ms, "1/2:scroll 5:debug");
}

/**
 * @brief 在 OLED 可接收新显存时绘制并提交一帧
 * @param menu 菜单实例
 * @param now_ms 当前单调毫秒时间
 * @retval STATUS_OK 帧已提交或显示尚忙而安全跳过
 * @retval STATUS_UNAVAILABLE 显示端口不可用
 * @retval STATUS_IO_ERROR 清屏、像素写入或刷新提交失败
 */
static status_code_t menu_service_render(menu_service_t *menu, uint32_t now_ms)
{
    bool is_ready;
    uint32_t period_ms;
    status_code_t status;

    status = menu->config.port.frame_ready(menu->config.port.context, &is_ready);
    if (status != STATUS_OK) {
        return status;
    }
    if (!is_ready) {
        return STATUS_OK;
    }
    status = menu->config.port.clear(menu->config.port.context);
    if (status != STATUS_OK) {
        return status;
    }
    if (menu->mode == MENU_SERVICE_MODE_LIVE) {
        status = menu_service_render_live(menu, now_ms);
    } else {
        switch (menu->debug_state) {
            case MENU_SERVICE_DEBUG_GROUP:
                status = menu_service_render_group(menu, now_ms);
                break;
            case MENU_SERVICE_DEBUG_PARAMETER:
                status = menu_service_render_parameter(menu, now_ms);
                break;
            case MENU_SERVICE_DEBUG_EDIT:
            case MENU_SERVICE_DEBUG_STEP:
                status = menu_service_render_edit(menu, now_ms);
                break;
            default:
                return STATUS_STATE_ERROR;
        }
    }
    if (status != STATUS_OK) {
        return status;
    }
    status = menu->config.port.refresh(menu->config.port.context);
    if (status != STATUS_OK) {
        return status;
    }
    period_ms = menu->mode == MENU_SERVICE_MODE_LIVE ? menu->config.live_refresh_period_ms
                                                     : menu->config.debug_refresh_period_ms;
    menu->next_refresh_ms = now_ms + period_ms;
    menu->is_dirty = false;
    return STATUS_OK;
}

/**
 * @brief 将按键时序转换为至多一个菜单动作
 * @param menu 菜单实例
 * @param now_ms 当前单调毫秒时间
 * @param key_state 五键稳定按下位掩码
 * @return 唯一无歧义动作；无动作或 K1–K4 同周期多动作时返回 NONE
 */
static menu_service_action_t menu_service_collect_action(menu_service_t *menu, uint32_t now_ms,
    uint8_t key_state)
{
    menu_service_action_t action = MENU_SERVICE_ACTION_NONE;
    uint8_t action_count = 0U;
    uint8_t pressed = (uint8_t)(key_state & (uint8_t)~menu->previous_key_state);
    uint8_t released = (uint8_t)(menu->previous_key_state & (uint8_t)~key_state);
    uint8_t control_state = key_state & MENU_SERVICE_CONTROL_KEY_MASK;
    uint8_t key_index;

    for (key_index = 0U; key_index < MENU_SERVICE_KEY_COUNT; key_index++) {
        uint8_t mask = (uint8_t)(1U << key_index);

        if ((pressed & mask) != 0U) {
            menu->pressed_at_ms[key_index] = now_ms;
            menu->repeat_at_ms[key_index] = now_ms + menu->config.repeat_delay_ms;
            menu->long_emitted_mask &= (uint8_t)~mask;
        }
    }
    menu->previous_key_state = key_state;
    if ((control_state != 0U) &&
        ((control_state & (uint8_t)(control_state - 1U)) != 0U)) {
        menu->is_key_chord_blocked = true;
    }
    if ((pressed & (uint8_t)(1U << MENU_SERVICE_KEY_MODE)) != 0U) {
        if (control_state != 0U) {
            menu->is_key_chord_blocked = true;
        }
        return MENU_SERVICE_ACTION_MODE;
    }
    if (menu->is_key_chord_blocked) {
        if (control_state == 0U) {
            menu->is_key_chord_blocked = false;
        }
        return MENU_SERVICE_ACTION_NONE;
    }
    if ((pressed & (uint8_t)(1U << MENU_SERVICE_KEY_UP)) != 0U) {
        action = MENU_SERVICE_ACTION_UP;
        action_count++;
    }
    if ((pressed & (uint8_t)(1U << MENU_SERVICE_KEY_DOWN)) != 0U) {
        action = MENU_SERVICE_ACTION_DOWN;
        action_count++;
    }
    if ((released & (uint8_t)(1U << MENU_SERVICE_KEY_CONFIRM)) != 0U) {
        if ((menu->long_emitted_mask & (uint8_t)(1U << MENU_SERVICE_KEY_CONFIRM)) == 0U) {
            action = MENU_SERVICE_ACTION_CONFIRM;
            action_count++;
        }
        menu->long_emitted_mask &= (uint8_t)~(1U << MENU_SERVICE_KEY_CONFIRM);
    }
    if ((released & (uint8_t)(1U << MENU_SERVICE_KEY_CANCEL)) != 0U) {
        action = MENU_SERVICE_ACTION_CANCEL;
        action_count++;
    }
    if ((key_state & (uint8_t)(1U << MENU_SERVICE_KEY_CONFIRM)) != 0U) {
        uint8_t mask = (uint8_t)(1U << MENU_SERVICE_KEY_CONFIRM);

        if (((menu->long_emitted_mask & mask) == 0U) &&
            ((uint32_t)(now_ms - menu->pressed_at_ms[MENU_SERVICE_KEY_CONFIRM]) >=
                menu->config.long_press_ms)) {
            menu->long_emitted_mask |= mask;
            action = MENU_SERVICE_ACTION_CONFIRM_LONG;
            action_count++;
        }
    }
    for (key_index = MENU_SERVICE_KEY_UP; key_index <= MENU_SERVICE_KEY_DOWN; key_index++) {
        uint8_t mask = (uint8_t)(1U << key_index);

        if (((key_state & mask) != 0U) &&
            menu_service_deadline_reached(now_ms, menu->repeat_at_ms[key_index])) {
            uint32_t elapsed_periods =
                (now_ms - menu->repeat_at_ms[key_index]) / menu->config.repeat_period_ms;

            menu->repeat_at_ms[key_index] +=
                (elapsed_periods + 1U) * menu->config.repeat_period_ms;
            action = key_index == MENU_SERVICE_KEY_UP ? MENU_SERVICE_ACTION_UP
                                                      : MENU_SERVICE_ACTION_DOWN;
            action_count++;
        }
    }
    return action_count == 1U ? action : MENU_SERVICE_ACTION_NONE;
}

/**
 * @brief 进入当前选中参数的草稿编辑页
 * @param menu 菜单实例
 * @param now_ms 当前单调毫秒时间
 */
static void menu_service_enter_edit(menu_service_t *menu, uint32_t now_ms)
{
    status_code_t status = parameter_service_get(menu->config.parameters, menu->group_index,
        menu->parameter_index, &menu->edit_value);

    if (status != STATUS_OK) {
        menu_service_notify(menu, now_ms, "Read failed");
        return;
    }
    menu->original_value = menu->edit_value;
    menu->step_cache_index = menu_service_linear_index(menu->config.parameters,
        menu->group_index, menu->parameter_index);
    menu->debug_state = MENU_SERVICE_DEBUG_EDIT;
    menu->is_dirty = true;
}

/**
 * @brief 处理 Debug 模式的一个抽象动作
 * @param menu 菜单实例
 * @param action 待处理动作
 * @param now_ms 当前单调毫秒时间
 */
static void menu_service_handle_debug(menu_service_t *menu, menu_service_action_t action,
    uint32_t now_ms)
{
    const parameter_service_group_t *group =
        &menu->config.parameters->groups[menu->group_index];
    const parameter_service_descriptor_t *descriptor;
    scalar_value_t current_value;
    status_code_t status;

    switch (menu->debug_state) {
        case MENU_SERVICE_DEBUG_GROUP:
            if (action == MENU_SERVICE_ACTION_CONFIRM) {
                menu->parameter_index = 0U;
                menu->parameter_scroll = 0U;
                menu->debug_state = MENU_SERVICE_DEBUG_PARAMETER;
                menu->is_dirty = true;
            } else if (action == MENU_SERVICE_ACTION_UP) {
                menu->group_index = menu->group_index == 0U
                                        ? menu->config.parameters->group_count - 1U
                                        : menu->group_index - 1U;
                menu_service_adjust_scroll(menu->group_index,
                    menu->config.parameters->group_count, &menu->group_scroll);
                menu->is_dirty = true;
            } else if (action == MENU_SERVICE_ACTION_DOWN) {
                menu->group_index =
                    (menu->group_index + 1U) % menu->config.parameters->group_count;
                menu_service_adjust_scroll(menu->group_index,
                    menu->config.parameters->group_count, &menu->group_scroll);
                menu->is_dirty = true;
            }
            break;
        case MENU_SERVICE_DEBUG_PARAMETER:
            if (action == MENU_SERVICE_ACTION_CONFIRM) {
                menu_service_enter_edit(menu, now_ms);
            } else if (action == MENU_SERVICE_ACTION_CANCEL) {
                menu->debug_state = MENU_SERVICE_DEBUG_GROUP;
                menu->is_dirty = true;
            } else if (action == MENU_SERVICE_ACTION_UP) {
                menu->parameter_index = menu->parameter_index == 0U
                                            ? group->parameter_count - 1U
                                            : menu->parameter_index - 1U;
                menu_service_adjust_scroll(menu->parameter_index, group->parameter_count,
                    &menu->parameter_scroll);
                menu->is_dirty = true;
            } else if (action == MENU_SERVICE_ACTION_DOWN) {
                menu->parameter_index = (menu->parameter_index + 1U) % group->parameter_count;
                menu_service_adjust_scroll(menu->parameter_index, group->parameter_count,
                    &menu->parameter_scroll);
                menu->is_dirty = true;
            }
            break;
        case MENU_SERVICE_DEBUG_EDIT:
            descriptor = &group->parameters[menu->parameter_index];
            if (action == MENU_SERVICE_ACTION_CONFIRM) {
                status = parameter_service_get(menu->config.parameters, menu->group_index,
                    menu->parameter_index, &current_value);
                if (status != STATUS_OK) {
                    menu_service_notify(menu, now_ms, "Read failed");
                } else if (!menu_service_value_is_equal(&current_value,
                               &menu->original_value)) {
                    menu->edit_value = current_value;
                    menu->original_value = current_value;
                    menu_service_notify(menu, now_ms, "Conflict: reloaded");
                } else {
                    status = parameter_service_set(menu->config.parameters, menu->group_index,
                        menu->parameter_index, &menu->edit_value);
                    if (status == STATUS_OK) {
                        menu->debug_state = MENU_SERVICE_DEBUG_PARAMETER;
                        menu->is_dirty = true;
                    } else {
                        menu_service_notify(menu, now_ms, "Apply failed");
                    }
                }
            } else if ((action == MENU_SERVICE_ACTION_CONFIRM_LONG) &&
                       (descriptor->type != SCALAR_VALUE_BOOL)) {
                menu->original_step = menu->step_cache[menu->step_cache_index];
                menu->debug_state = MENU_SERVICE_DEBUG_STEP;
                menu->is_dirty = true;
            } else if (action == MENU_SERVICE_ACTION_CANCEL) {
                menu->debug_state = MENU_SERVICE_DEBUG_PARAMETER;
                menu->is_dirty = true;
            } else if (action == MENU_SERVICE_ACTION_UP) {
                menu_service_adjust_draft(descriptor,
                    &menu->step_cache[menu->step_cache_index], 1, &menu->edit_value);
                menu->is_dirty = true;
            } else if (action == MENU_SERVICE_ACTION_DOWN) {
                menu_service_adjust_draft(descriptor,
                    &menu->step_cache[menu->step_cache_index], -1, &menu->edit_value);
                menu->is_dirty = true;
            }
            break;
        case MENU_SERVICE_DEBUG_STEP:
            if (action == MENU_SERVICE_ACTION_CONFIRM) {
                menu->debug_state = MENU_SERVICE_DEBUG_EDIT;
                menu->is_dirty = true;
            } else if (action == MENU_SERVICE_ACTION_CANCEL) {
                menu->step_cache[menu->step_cache_index] = menu->original_step;
                menu->debug_state = MENU_SERVICE_DEBUG_EDIT;
                menu->is_dirty = true;
            } else if (action == MENU_SERVICE_ACTION_UP) {
                menu_service_scale_step(1, &menu->step_cache[menu->step_cache_index]);
                menu->is_dirty = true;
            } else if (action == MENU_SERVICE_ACTION_DOWN) {
                menu_service_scale_step(-1, &menu->step_cache[menu->step_cache_index]);
                menu->is_dirty = true;
            }
            break;
        default:
            menu->debug_state = MENU_SERVICE_DEBUG_GROUP;
            menu->is_dirty = true;
            break;
    }
}

/**
 * @brief 处理当前显示模式的一个抽象动作
 * @param menu 菜单实例
 * @param action 待处理动作
 * @param now_ms 当前单调毫秒时间
 */
static void menu_service_handle_action(menu_service_t *menu, menu_service_action_t action,
    uint32_t now_ms)
{
    if (action == MENU_SERVICE_ACTION_MODE) {
        menu->mode = menu->mode == MENU_SERVICE_MODE_DEBUG ? MENU_SERVICE_MODE_LIVE
                                                           : MENU_SERVICE_MODE_DEBUG;
        menu->is_dirty = true;
        return;
    }
    if (menu->mode == MENU_SERVICE_MODE_DEBUG) {
        menu_service_handle_debug(menu, action, now_ms);
        return;
    }
    if ((action == MENU_SERVICE_ACTION_UP) &&
        (menu->config.live_item_count > MENU_SERVICE_VISIBLE_ITEM_COUNT)) {
        menu->live_scroll = menu->live_scroll == 0U ? menu->config.live_item_count - 1U
                                                    : menu->live_scroll - 1U;
        menu->is_dirty = true;
    } else if ((action == MENU_SERVICE_ACTION_DOWN) &&
               (menu->config.live_item_count > MENU_SERVICE_VISIBLE_ITEM_COUNT)) {
        menu->live_scroll = (menu->live_scroll + 1U) % menu->config.live_item_count;
        menu->is_dirty = true;
    }
}

/**
 * @brief 验证菜单端口、刷新周期和实时数据注册表
 * @param config 待验证配置
 * @retval STATUS_OK 配置完整且资源有界
 * @retval STATUS_INVALID_ARGUMENT 端口、数组、周期或元数据无效
 * @retval STATUS_OUT_OF_RANGE 参数数超过步长缓存容量
 * @retval STATUS_STATE_ERROR 实时数据项 ID 重复
 */
static status_code_t menu_service_validate_config(const menu_service_config_t *config)
{
    size_t item_index;
    size_t earlier_index;

    if (!config || !config->parameters || !config->parameters->is_initialized ||
        !config->live_items || (config->live_item_count == 0U) ||
        !config->port.get_time || !config->port.get_keys || !config->port.frame_ready ||
        !config->port.clear || !config->port.set_pixel || !config->port.refresh ||
        (config->debug_refresh_period_ms == 0U) || (config->live_refresh_period_ms == 0U) ||
        (config->long_press_ms == 0U) || (config->repeat_delay_ms == 0U) ||
        (config->repeat_period_ms == 0U)) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (config->parameters->parameter_count > MENU_SERVICE_STEP_CACHE_CAPACITY) {
        return STATUS_OUT_OF_RANGE;
    }
    for (item_index = 0U; item_index < config->live_item_count; item_index++) {
        const menu_service_live_item_t *item = &config->live_items[item_index];

        if (!item->name || (item->name[0] == '\0') || !item->unit || !item->read ||
            !menu_service_type_is_valid(item->type) ||
            (item->decimals > PARAMETER_SERVICE_DECIMAL_CAPACITY) ||
            ((item->type != SCALAR_VALUE_FLOAT) && (item->decimals != 0U))) {
            return STATUS_INVALID_ARGUMENT;
        }
        for (earlier_index = 0U; earlier_index < item_index; earlier_index++) {
            if (config->live_items[earlier_index].id == item->id) {
                return STATUS_STATE_ERROR;
            }
        }
    }
    return STATUS_OK;
}

/**
 * @brief 初始化菜单实例并建立每参数步长缓存
 * @param menu 由调用者静态持有的菜单实例
 * @param config 生命周期和端口配置，复制结构体但借用其指向对象
 * @retval STATUS_OK 菜单已进入默认 Debug L1 页
 * @retval STATUS_INVALID_ARGUMENT 实例、端口、周期或注册元数据无效
 * @retval STATUS_OUT_OF_RANGE 参数数超过步长缓存容量
 * @retval STATUS_STATE_ERROR 实时数据项 ID 重复
 */
status_code_t menu_service_init(menu_service_t *menu, const menu_service_config_t *config)
{
    const parameter_service_descriptor_t *descriptor;
    uint32_t now_ms;
    uint8_t key_state = 0U;
    size_t group_index;
    size_t parameter_index;
    size_t linear_index = 0U;
    size_t key_index;
    status_code_t status;

    if (!menu) {
        return STATUS_INVALID_ARGUMENT;
    }
    menu->is_initialized = false;
    status = menu_service_validate_config(config);
    if (status != STATUS_OK) {
        return status;
    }
    status = config->port.get_time(config->port.context, &now_ms);
    if (status != STATUS_OK) {
        return status;
    }
    menu->config = *config;
    for (group_index = 0U; group_index < config->parameters->group_count; group_index++) {
        for (parameter_index = 0U;
             parameter_index < config->parameters->groups[group_index].parameter_count;
             parameter_index++) {
            status = parameter_service_get_descriptor(config->parameters, group_index,
                parameter_index, &descriptor);
            if (status != STATUS_OK) {
                return status;
            }
            menu->step_cache[linear_index] = descriptor->step;
            linear_index++;
        }
    }
    (void)memset(&menu->edit_value, 0, sizeof(menu->edit_value));
    (void)memset(&menu->original_value, 0, sizeof(menu->original_value));
    (void)memset(&menu->original_step, 0, sizeof(menu->original_step));
    menu->mode = MENU_SERVICE_MODE_DEBUG;
    menu->debug_state = MENU_SERVICE_DEBUG_GROUP;
    menu->group_index = 0U;
    menu->parameter_index = 0U;
    menu->group_scroll = 0U;
    menu->parameter_scroll = 0U;
    menu->live_scroll = 0U;
    menu->step_cache_index = 0U;
    menu->next_refresh_ms = now_ms;
    menu->notification_until_ms = now_ms;
    menu->notification[0] = '\0';
    menu->long_emitted_mask = 0U;
    menu->is_key_chord_blocked = false;
    menu->is_dirty = true;
    status = config->port.get_keys(config->port.context, &key_state);
    if (status != STATUS_OK) {
        key_state = 0U;
    }
    menu->previous_key_state = key_state;
    for (key_index = 0U; key_index < MENU_SERVICE_KEY_COUNT; key_index++) {
        menu->pressed_at_ms[key_index] = now_ms;
        menu->repeat_at_ms[key_index] = now_ms + config->repeat_delay_ms;
    }
    menu->is_initialized = true;
    return STATUS_OK;
}

/**
 * @brief 读取按键、推进状态并在到期且 OLED 就绪时提交一帧
 * @param menu 已初始化菜单实例
 * @retval STATUS_OK 状态已推进，未到周期或 OLED 忙也视为成功
 * @retval STATUS_INVALID_ARGUMENT menu 为空
 * @retval STATUS_NOT_INITIALIZED 菜单尚未初始化
 * @retval STATUS_UNAVAILABLE 时间、按键或显示端口不可用
 * @retval STATUS_IO_ERROR 端口采集或提交帧时报错
 */
status_code_t menu_service_process(menu_service_t *menu)
{
    menu_service_action_t action;
    uint32_t now_ms;
    uint8_t key_state;
    status_code_t key_status;
    status_code_t status;

    if (!menu) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!menu->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    status = menu->config.port.get_time(menu->config.port.context, &now_ms);
    if (status != STATUS_OK) {
        return status;
    }
    key_status = menu->config.port.get_keys(menu->config.port.context, &key_state);
    if (key_status == STATUS_OK) {
        action = menu_service_collect_action(menu, now_ms, key_state);
        if (action != MENU_SERVICE_ACTION_NONE) {
            menu_service_handle_action(menu, action, now_ms);
        }
    }
    if ((menu->notification[0] != '\0') &&
        menu_service_deadline_reached(now_ms, menu->notification_until_ms)) {
        menu->notification[0] = '\0';
        menu->is_dirty = true;
    }
    if (menu->is_dirty || menu_service_deadline_reached(now_ms, menu->next_refresh_ms)) {
        status = menu_service_render(menu, now_ms);
        if (status != STATUS_OK) {
            return status;
        }
    }
    return key_status;
}

/**
 * @brief 读取当前菜单导航、模式和草稿状态快照
 * @param menu 已初始化菜单实例
 * @param snapshot 接收快照的存储地址
 * @retval STATUS_OK 快照已写入
 * @retval STATUS_INVALID_ARGUMENT 任一参数为空
 * @retval STATUS_NOT_INITIALIZED 菜单尚未初始化
 */
status_code_t menu_service_get_snapshot(const menu_service_t *menu,
    menu_service_snapshot_t *snapshot)
{
    if (!menu || !snapshot) {
        return STATUS_INVALID_ARGUMENT;
    }
    if (!menu->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    snapshot->mode = menu->mode;
    snapshot->debug_state = menu->debug_state;
    snapshot->group_index = menu->group_index;
    snapshot->parameter_index = menu->parameter_index;
    snapshot->live_scroll = menu->live_scroll;
    snapshot->edit_value = menu->edit_value;
    snapshot->has_draft = (menu->debug_state == MENU_SERVICE_DEBUG_EDIT) ||
                          (menu->debug_state == MENU_SERVICE_DEBUG_STEP);
    return STATUS_OK;
}
