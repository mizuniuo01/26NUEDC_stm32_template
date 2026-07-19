# 嵌入式 C 代码风格规范

> 版本：v1.2.0<br>
> 创建日期：2026-07-18<br>
> 最后更新：2026-07-19<br>
> 状态：Active<br>
> 适用范围：个人 STM32、TI MSPM0、ESP32 及其他 MCU 的 C 项目

本文档只规定嵌入式 C 的代码表达方式和可执行编码规则。软件分层、模块边界、状态机、通信架构、裸机/RTOS 运行模型等内容见
[ARCHITECTURE_STANDARD.md](ARCHITECTURE_STANDARD.md)。Python 遵循 PEP 8，不在本文档维护 Python 细则；本文档不覆盖 C++。

本规范参考 BARR-C:2018 与 Linux 内核编码规范，并结合个人嵌入式开发实践进行取舍；存在冲突时，以本文档的明确规则为准。本规范不声明完全兼容上述规范。

## 1. 使用方式

### 1.1 规则等级

- **MUST**：默认必须遵守；违反时应在提交前修复。
- **SHOULD**：通常应遵守；有明确技术原因时可以例外。
- **MAY**：可选实践，由项目规模和工具链决定。
- **EXCEPTION**：经过记录、评估和验证后允许的例外。

外部代码按以下顺序处理：芯片厂商生成代码、CMSIS、HAL 和第三方库保持其原有风格；不得为了套用本文档直接修改外部代码。通过 BSP、驱动或适配层接入外部代码，手写的业务和平台封装代码遵循本文档。

### 1.2 可维护性原则

1. 代码首先服务于阅读、调试和验证，其次才是减少行数。
2. 同一仓库只采用一种写法；已有项目的迁移应按模块逐步完成，不在一次提交中混合格式化和功能改动。
3. 规则尽量由编译器、clang-format、静态分析或 CI 检查执行；工具无法判断的规则必须通过代码审查确认。
4. 任何例外都写明原因、影响范围和失效条件，不在代码中留下无法解释的特殊写法。

### 1.3 维护与版本

本文档采用 SemVer：

- `MAJOR`：规则体系或核心风格发生不兼容变化。
- `MINOR`：增加规则或工具支持，但不改变已有规则含义。
- `PATCH`：修正文字、示例、链接或排版。

新增规则放入已有主题章节；只有主题确实独立时才新增章节。废弃规则先标记为 `Deprecated`，并在变更记录中给出替代规则和计划移除版本。

### 1.4 C 语言基线

- 个人项目默认使用 ISO C11；构建系统应显式指定语言标准，不依赖编译器默认值。
- 厂商 SDK、HAL 或工具链需要 GNU 扩展时，可以使用 `gnu11`，但扩展只能集中在 HAL、BSP、驱动或平台兼容层。
- 旧编译器只能完整支持 C99 时，可以将项目基线降为 C99，并在 README 或构建配置中记录。
- `.c` 文件必须由工具链的 C 前端按所选 C 标准编译，禁止使用 C++ 前端解释 C 源码。
- 不使用 `#define` 重命名 C 关键字或构造伪语言。
- `#pragma`、编译器属性、内联汇编和专有关键字保持最少，并由平台封装隔离。

安全关键项目不能仅依赖本文档；应根据风险等级进一步采用 MISRA C、静态分析和正式的偏离审批。

## 2. 命名

### 2.1 基本形式

| 对象 | 形式 | 示例 |
| --- | --- | --- |
| 宏、编译选项、枚举成员 | `UPPER_SNAKE_CASE` | `MOTOR_MAX_SPEED` |
| 变量、函数、文件 | `snake_case` | `motor_position` |
| 类型 | `snake_case_t` | `motor_state_t` |
| 结构体标签 | `snake_case` | `struct motor_context` |
| 私有文件级符号 | `snake_case` + `static` | `static uint8_t rx_state` |
| 中断处理函数 | 工具链/芯片要求的名称 | `USART1_IRQHandler` |

公共函数、公共类型、公共枚举和公共变量必须带模块前缀；模块私有的 `static` 函数可以省略前缀，但同一文件内仍应保持语义清晰。

```c
typedef enum {
    MOTOR_STATE_IDLE,
    MOTOR_STATE_RUNNING,
    MOTOR_STATE_FAULT,
} motor_state_t;

motor_status_t motor_set_speed(motor_handle_t *motor, int16_t speed);
```

### 2.2 词汇与缩写

- 禁止在同一项目中混用 `timeout`/`time_out`、`position`/`pos` 等同义写法。
- 允许使用 `rx`、`tx`、`id`、`cfg`、`ctx`、`buf`、`len`、`cnt`、`idx`、`err` 等行业通用缩写。
- 新缩写首次出现时在类型、注释或模块说明中解释；不要为了缩短名称删除关键语义。
- 时间、长度、角度、频率等单位写入名称：`timeout_ms`、`sample_hz`、`angle_deg`、`buffer_size`。
- 布尔变量使用 `is_`、`has_`、`can_` 或 `should_` 等能表达真假语义的前缀。

### 2.3 文件名和头文件保护

文件使用全小写 `snake_case.c`、`snake_case.h`；一对接口和实现文件名称必须完全相同，不使用空格、连接符或仅大小写不同的名称。

```c
#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

/* 接口内容 */

#endif /* MOTOR_CONTROLLER_H */
```

默认使用标准头文件保护，不使用 `#pragma once`。如果工具链或生成器强制使用其他形式，应在项目 README 或例外记录中说明。

### 2.4 宏、常量和配置项命名

宏和编译期开关使用 `UPPER_SNAKE_CASE`。功能开关采用肯定语义，值必须明确为 `0` 或 `1`，不要通过“是否定义”表达同一项目中的普通配置。

```c
#define CONFIG_MOTOR_USE_CURRENT_LIMIT 1
#define MOTOR_CONTROL_PERIOD_MS        5U
#define GYRO_MAX_DELTA_DEG             50.0F
```

规则：

- 公共宏带模块前缀；仅在一个 `.c` 使用的宏仍放在该文件内。
- 表示数量的名称使用 `_COUNT`，容量使用 `_CAPACITY`，字节长度使用 `_SIZE_BYTES`。
- 位掩码以 `_MASK` 结尾，位编号以 `_BIT` 结尾。
- 超时必须带单位，如 `_TIMEOUT_MS`；不得使用含义不清的 `_TIME`。
- 不把普通变量伪装成全大写“常量”；只读运行时对象使用 `const` 和普通变量命名。
- 不在公共头文件定义无模块前缀的 `MIN`、`MAX`、`BIT` 等通用宏。

### 2.5 函数、回调和任务命名

函数名以动词开头，并让调用者能从名称判断动作和阻塞语义：

| 类别 | 推荐形式 | 示例 |
| --- | --- | --- |
| 生命周期 | `<module>_init/deinit/start/stop/reset` | `imu_start()` |
| 配置 | `<module>_configure/set/get` | `motor_set_limit()` |
| 非阻塞推进 | `<module>_task/process/poll` | `protocol_process()` |
| 查询 | `<module>_is/has/can` | `uart_is_busy()` |
| ISR 入口 | `<module>_on_<event>_isr` | `uart_on_rx_isr()` |
| 普通回调 | `<module>_on_<event>` | `button_on_pressed()` |
| 转换 | `<source>_to_<target>` | `ticks_to_ms()` |

`task` 表示由调度器反复调用且必须非阻塞；`process` 表示消费已有输入；`poll` 表示主动检查一次状态；`wait`、`read_blocking` 等名称必须明确表示可能阻塞。

### 2.6 类型和实例命名

- 配置类型使用 `<module>_config_t`，实例/句柄使用 `<module>_handle_t` 或 `<module>_t`。
- 状态、事件、命令、错误分别使用 `_state_t`、`_event_t`、`_command_t`、`_status_t`。
- 类型名称表达抽象含义，不包含无必要的平台名称；平台适配类型可使用 `stm32_`、`mspm0_` 等前缀。
- 不额外给全局变量加 `g_`，而是通过 `static`、模块 API 和作用域控制可见性。
- 单字母变量仅用于极短循环索引或数学公式；生命周期超过几行时使用语义名称。

### 2.7 保留标识符与名称冲突

- 项目标识符不得以双下划线开头，也不得以下划线后接大写字母开头。
- 文件作用域的项目标识符不得以下划线开头；为保持一致，其他作用域也不自行创建下划线前缀名称。
- 不定义与 C 标准库函数、宏、类型或全局对象重名的符号，例如 `strlen`、`errno`、`assert`。
- 不复用编译器、CMSIS、HAL、RTOS、芯片厂商和第三方库保留的前缀。
- 中断入口、启动符号和生成器要求的名称属于平台例外，必须保持其权威拼写并隔离在平台层。
- 新模块选择公共前缀前，应搜索仓库和依赖，确认不存在同名公共符号。

## 3. 格式化

### 3.1 缩进和括号

- 缩进 4 个空格，禁止 Tab。
- 控制流使用 K&R 风格；函数定义使用函数名单独一行的风格。
- 所有控制流都必须使用花括号，即使只有一条语句。
- 一条语句独占一行。

```c
if (motor_is_ready(motor)) {
    motor_start(motor);
}

void motor_task(void)
{
    motor_update();
}
```

### 3.2 列宽和续行

普通代码列宽上限为 **100 个字符**；超过上限必须在语义自然的位置换行。宏定义的代码和行尾注释也应尽量控制在 100 列以内，不能以列宽为理由截断标识符。

- 函数参数过多时，每个参数单独一行或按工具格式化结果排列。
- 长条件在低优先级逻辑运算符后换行。
- 长算术表达式在运算符后换行。
- 三元表达式的 `?`、`:` 分行时保持视觉层次一致。
- 长字符串使用相邻字符串拼接，不依赖超长单行。
- 续行缩进 4 个空格，不人为制造跨文件不一致的对齐。

```c
if ((sensor_value > threshold_high)
    && (system_state == SYSTEM_STATE_ACTIVE)
    && safety_check_passed()) {
    control_enable();
}
```

### 3.3 空格、空行和对齐

- 二元运算符、赋值运算符和逗号后使用空格。
- 函数名与左括号之间不加空格；控制关键字与左括号之间加空格。
- 一元运算符、数组下标、`.` 和 `->` 两侧不加空格。
- 函数之间使用一个空行；逻辑段落之间最多使用一个空行。
- 文件末尾必须且只能有一个换行符，禁止行尾空白。
- 连续声明或赋值只有在确实属于同一组时才手动对齐，不为了对齐而破坏自动格式化。

### 3.4 `switch` 和条件表达式

- `case` 缩进一层，语句再缩进一层。
- `default` 必须存在，除非编译器或协议定义已证明枚举值完整且代码中有明确说明。
- 每个 `case` 默认以 `break`、`return` 或明确的状态转移结束。
- 故意贯穿必须写 `/* fall through */`。
- `if` 条件中禁止赋值；先完成赋值，再判断结果。
- 复杂表达式使用括号明确优先级，不依赖读者记忆 C 运算符优先级。

```c
switch (motor->state) {
    case MOTOR_STATE_IDLE:
        motor_stop_output(motor);
        break;

    case MOTOR_STATE_RUNNING:
        motor_update_output(motor);
        break;

    case MOTOR_STATE_FAULT:
        motor_disable_output(motor);
        break;

    default:
        motor->state = MOTOR_STATE_FAULT;
        break;
}
```

### 3.5 声明、指针和初始化器

- 一行只声明一个变量，避免 `int32_t *a, b;` 造成指针语义混淆。
- 指针星号靠变量名：`uint8_t *buffer`。
- 变量在最小可用作用域内声明，并尽量在声明处获得有意义的初值。
- 不为了“变量都在块开头”扩大变量生命周期。
- 数组、结构体和枚举初始化器保留尾逗号，便于后续追加和减少 diff。
- 指定初始化优先于依赖成员顺序的初始化。

```c
motor_config_t motor_config = {
    .pwm_channel = PWM_CHANNEL_LEFT,
    .direction_pin = GPIO_PIN_MOTOR_LEFT_DIR,
    .max_speed = 2000,
};
```

### 3.6 函数声明和调用换行

参数能在 100 列内表达时保持单行；超过列宽时按参数边界换行。声明和定义必须使用相同的逻辑分组，不为了减少行数把类型与变量名拆开。

```c
motor_status_t motor_configure(motor_handle_t *motor,
                               const motor_config_t *config,
                               const motor_port_t *port);

status = protocol_decode_frame(&decoder,
                               rx_buffer,
                               rx_length,
                               &decoded_frame);
```

长表达式的换行应让操作顺序清楚：

```c
uint32_t compensated_value = base_value
                             + calculate_offset(&calibration)
                             + (temperature_delta * compensation_factor);
```

### 3.7 预处理指令

预处理指令的 `#` 顶格，条件内代码按正常代码缩进。每个 `#else`、`#elif` 和较远的 `#endif` 注明对应条件。条件编译尽量集中在平台适配边界，嵌套不超过两层。

```c
#if CONFIG_UART_USE_DMA
    uart_dma_start(&uart);
#else /* CONFIG_UART_USE_DMA */
    uart_interrupt_start(&uart);
#endif /* CONFIG_UART_USE_DMA */
```

不要用条件编译复制大段业务逻辑；应选择不同适配器实现，再由统一接口调用。

### 3.8 格式化禁用区

只有寄存器表、协议字段表、需要列对齐的查找表等自动格式化会显著降低可读性的区域，才允许使用 `clang-format off/on`。禁用区必须尽量小，并说明原因。

```c
/* clang-format off: 协议字段必须与文档中的字节布局逐列对应 */
static const protocol_field_t fields[] = {
    { FRAME_FIELD_COMMAND,  0U, 1U },
    { FRAME_FIELD_LENGTH,   1U, 2U },
    { FRAME_FIELD_PAYLOAD,  3U, 8U },
};
/* clang-format on */
```

### 3.9 编码、行尾和不可打印字符

- 所有手写源码和文本配置使用 UTF-8；默认不带 BOM。
- 行尾必须使用 LF（ASCII `0x0A`），禁止把 CRLF 混入仓库。
- 除换行外，源码不得包含控制字符或不可打印字符；字符串中的 Tab 使用转义序列 `\t`。
- 文件末尾有且仅有一个 LF，禁止尾随空白和多余空行。
- 生成代码无法满足上述规则时，不直接修改生成文件；在 `.gitattributes`、生成步骤或检查范围中记录例外。

推荐在仓库根目录使用 `.editorconfig` 和 `.gitattributes` 固化 UTF-8、LF、缩进及文件末尾换行，CI 使用 `git diff --check` 检测尾随空白。

## 4. 注释与接口文档

### 4.1 注释原则

注释默认使用中文，说明“为什么”、硬件限制、时序要求、单位、所有权和异常条件。不要用注释复述明显的代码行为，也不要在注释中向 AI 或其他人发送临时讨论。

### 4.2 公共 API

公共 API 在头文件中必须说明契约；私有函数在实现文件中按复杂度决定是否写注释。接口说明至少覆盖必要字段：

```c
/**
 * @brief  启动一次非阻塞 ADC 采样。
 * @param  adc       已初始化的 ADC 实例。
 * @param  buffer    调用方提供的接收缓冲区。
 * @param  length    缓冲区可写元素数量。
 * @return 成功返回 ADC_STATUS_OK；忙、参数错误或硬件错误返回对应状态。
 * @note   函数不等待转换完成；完成事件由 adc_task() 消费。
 * @note   buffer 在完成事件前必须保持有效，不能在 ISR 中调用。
 */
adc_status_t adc_start(adc_handle_t *adc, uint16_t *buffer, size_t length);
```

### 4.3 文件和代码块

大型 `.c` 文件可以按“私有类型、私有变量、私有函数、公共 API、回调”分组；小文件不强制添加分隔线。文件头只记录模块用途、平台依赖和特殊限制，不记录会频繁变化的个人信息。

### 4.4 注释位置与行尾注释

普通解释写在被解释代码上方。行尾注释只用于短宏、枚举值、寄存器位或紧凑数据表；不要在普通语句右侧堆积长注释。

```c
/* 编码器安装方向相反，因此在进入控制算法前统一校正符号。 */
position = -position;

#define MOTOR_PWM_MAX 2000U /* 20 kHz 定时器周期对应的最大比较值 */
```

### 4.5 TODO 与临时标记

统一使用以下标签：

- `TODO(owner/date):` 尚未实现但不影响当前正确性。
- `FIXME(owner/date):` 已知缺陷，需要修复。
- `HACK(owner/date):` 有验证依据的临时绕过方案。
- `NOTE:` 容易被误改的重要事实。

标签必须说明触发条件或完成标准，不写“以后优化”之类不可执行文本。长期设计决定应进入文档或 ADR，不依赖 TODO 保存。

### 4.6 注释的错误用法

- 不保留大段被注释掉的旧代码，版本历史由 Git 保存。
- 不记录已经失效的历史讨论和调试输出。
- 不用注释掩盖糟糕命名。
- 不承诺代码没有实现的行为。
- 修改接口或时序时同步更新注释；错误注释比没有注释更危险。

## 5. 头文件与源文件

### 5.1 最小包含原则

- 头文件只包含其公共声明直接需要的类型。
- 能用前向声明时，不为实现细节引入完整头文件。
- `.c` 文件第一个 include 必须是本模块头文件，用于尽早发现头文件自洽性问题。
- 其余 include 按“项目接口、项目其他模块、第三方、标准库、平台/HAL”组织；项目可通过 clang-format 或 lint 保持稳定。

### 5.2 接口暴露

头文件只放公共类型、宏、枚举、常量、`extern` 声明和函数声明。实现变量、私有类型和内部辅助函数放在 `.c` 中，并使用 `static` 限制作用域。不要为了方便调试把整个内部结构体暴露给上层。

### 5.3 `const`、`volatile` 和所有权

- 不修改的输入指针使用 `const`。
- ISR、DMA 或其他并发上下文共享的对象才使用 `volatile`，并在注释中说明访问者。
- `volatile` 不保证复合操作的原子性，也不替代锁、临界区或内存屏障。
- 接口必须说明缓冲区由调用者还是模块拥有，谁负责释放或保持有效。

### 5.4 Include 顺序示例

```c
#include "motor.h"

#include "encoder.h"
#include "system_time.h"

#include "third_party/filter.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "stm32f4xx_hal.h"
```

分组之间使用一个空行。同一组顺序由项目统一；个人配置默认关闭 clang-format 自动排序，因为工具无法可靠区分项目、第三方和平台头文件。

### 5.5 头文件自洽与依赖泄漏

每个公共头文件必须能够被单独 include 并通过编译。头文件中出现某个类型时，要么包含该类型的权威头文件，要么使用合法的前向声明。禁止依赖“调用者碰巧先 include 了另一个文件”。

公共头文件不得包含 `main.h`、项目总头文件或仅为了获得一个平台宏而包含巨大的 HAL 聚合头。确实需要平台类型时，应考虑不透明句柄、端口接口或小型平台类型头。

### 5.6 全局变量与 `extern`

默认使用模块内部 `static` 状态和 API。`extern` 只用于工具链要求、启动文件、平台生成句柄或经过说明的极少数共享对象；声明只放在一个权威头文件，定义只存在一处。

ISR 与任务共享的标志也优先通过模块回调或事件接口封装。不得在多个头文件重复声明同一个全局对象，也不得暴露可由任意模块写入的大型状态结构体。

## 6. 类型、数据和表达式

### 6.1 固定宽度和布尔值

优先使用 `<stdint.h>` 的固定宽度类型和 `<stdbool.h>` 的 `bool`。只有在明确表达字符、字符串或标准库接口时使用 `char`。位操作使用无符号类型；避免在一个表达式中隐式混合有符号和无符号数。

### 6.2 枚举、宏和常量

概念上属于同一组的整数值使用枚举；浮点常量、带运算的预处理表达式和确实孤立的单值使用宏。宏参数和整体表达式必须加括号，多语句宏使用 `do { ... } while (0)`。

### 6.3 结构体和协议数据

硬件寄存器映射、通信帧和持久化数据必须显式考虑填充、对齐、大小端和版本兼容。禁止仅依赖编译器默认布局；需要 packed 时必须说明性能和未对齐访问风险，并优先使用显式序列化/反序列化。

### 6.4 初始化和表达式安全

- 局部变量在使用前初始化；不要用无意义初始化掩盖控制流问题。
- `sizeof` 优先对变量或成员使用。
- 强制类型转换必须有明确目的，特别是缩窄、符号变化和指针转换。
- 所有长度、索引、计数和移位操作都应有边界和溢出意识。
- 不使用未定义或实现定义行为作为协议或算法的一部分。

### 6.5 浮点与定点

浮点使用由 MCU、FPU、实时预算和数值需求共同决定。规则如下：

- 浮点字面量带 `F` 后缀，避免无意使用 `double`：`0.5F`。
- 无 FPU 或硬实时路径应测量浮点代价，必要时使用定点数。
- 定点类型在名称中标明比例或通过类型文档说明 Q 格式。
- 浮点比较使用容差，不直接比较计算结果是否相等。
- ISR 中只有在确认 FPU 上下文和最坏时间后才允许浮点运算。

```c
if (fabsf(measured_angle - target_angle) <= ANGLE_EPSILON_DEG) {
    state = CONTROL_STATE_SETTLED;
}
```

### 6.6 联合体和判别字段

联合体必须有明确的判别字段，读取前根据判别值确认当前有效成员。禁止由调用者凭隐式约定猜测有效成员。

```c
typedef enum {
    FRAME_PAYLOAD_COMMAND,
    FRAME_PAYLOAD_TELEMETRY,
} frame_payload_type_t;

typedef struct {
    frame_payload_type_t type;
    union {
        command_frame_t command;
        telemetry_frame_t telemetry;
    } payload;
} frame_t;
```

### 6.7 打包、对齐和序列化

协议默认采用逐字段序列化和解析，显式处理边界、字节序和未对齐访问。packed 结构体仅用于硬件描述或工具链 ABI 明确要求的场景，并用静态断言验证大小。

```c
_Static_assert(sizeof(protocol_header_t) == PROTOCOL_HEADER_SIZE,
               "protocol header layout changed");
```

### 6.8 `volatile`、原子与内存可见性

```c
static volatile bool dma_done; /* ISR 写，主循环读 */
```

单个对齐字长的读写是否原子取决于架构。读-改-写、多个字段组成的一致快照以及跨核访问仍需临界区、原子操作或锁。涉及 DMA 时还需考虑缓存一致性和内存屏障，不能只添加 `volatile`。

### 6.9 魔法数字与具名常量

硬件参数、协议值、超时、周期、限幅、数组容量、寄存器位和可调阈值不得以无名称字面量散落在逻辑中。应根据作用域和类型选择局部 `const`、文件级 `static const`、枚举、配置对象或宏。

以下字面量可以保留：

- 表达布尔、空值、初始索引和增量的 `0`、`1`。
- 数组或协议解析中含义直接且紧邻说明的偏移。
- 数学公式中公认且不会作为配置变化的系数。
- 位操作中清晰的单比特移位；位编号仍应在有硬件语义时具名。

```c
/* 禁止：无法判断 2000 和 5 的单位及来源。 */
if (speed > 2000) {
    timeout = 5;
}

/* 推荐：单位、作用域和语义明确。 */
if (speed > MOTOR_MAX_SPEED_RPM) {
    timeout_ms = MOTOR_STOP_TIMEOUT_MS;
}
```

不要为了消灭所有字面量创建 `ZERO`、`ONE`、`TWO` 等无语义名称，也不要把仅用于一个函数的实现细节提升为全局宏。

### 6.10 函数式宏

- 能用普通函数或 `static inline` 完成时，不使用函数式宏。
- 必须使用宏时，每个参数和整体结果都加括号。
- 宏参数只能求值一次；无法保证时，接口必须明确禁止带副作用的实参，并优先改成内联函数。
- 多语句宏使用 `do { ... } while (0)`，不得隐藏 `return`、`goto`、资源获取或不可见控制流。
- 宏不能依赖调用点恰好存在的局部变量。
- 需要类型安全、调试断点或地址语义的操作必须使用函数。

```c
static inline int32_t clamp_i32(int32_t value, int32_t minimum, int32_t maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}
```

## 7. 函数和控制流

- 一个函数只承担一个可解释的职责；过长时按数据流或错误边界拆分。
- 无参数函数显式使用 `(void)`。
- 参数过多时使用配置结构体，但不要为两个简单参数制造无意义的包装层。
- 公共 API 检查必要的指针和长度；私有函数通过前置条件减少重复防御。
- 失败必须通过返回值、状态对象或事件显式传播。
- 默认不使用递归和 VLA。
- 循环变量只在循环控制部分修改；等待循环必须有超时、事件或明确的硬件保证。
- `goto` 只用于函数末尾的统一清理路径，不用于构造普通循环或跨层跳转。

### 7.1 参数与返回值

- 输入参数在前，输出参数在后；输入结构体指针使用 `const`。
- 参数超过 4～5 个且属于同一配置时，SHOULD 使用配置结构体。
- 不用返回值同时表达“数据”和“错误”而导致歧义；使用状态码加输出参数，或返回带有效标志的结果类型。
- 返回模块所属的枚举状态，禁止使用无名称的 `-1`、`-2` 表达错误。
- 简单 getter 可以直接返回值，但数据可能无效时必须提供状态、时间戳或有效标志。

```c
sensor_status_t sensor_get_sample(const sensor_handle_t *sensor,
                                  sensor_sample_t *sample);
```

### 7.2 参数校验深度

| 接口位置 | 校验要求 |
| --- | --- |
| 公共初始化接口 | 检查空指针、范围、配置组合和依赖状态 |
| 外部数据入口 | 检查指针、长度、帧边界、校验和和版本 |
| 普通公共控制接口 | 检查对象状态和可能来自运行时的参数 |
| 模块私有函数 | 依赖明确前置条件，只保留关键防御 |
| ISR 回调 | 最小且确定时间的检查，不执行复杂恢复 |

参数来自编译期并不自动代表安全；只有调用边界和不变量真正受控时才能省略范围检查。安全限幅还必须在最终输出边界再次执行。

### 7.3 空指针与布尔判断

布尔值直接判断，数值与零显式比较。指针判断项目内统一使用简洁形式：

```c
if (!motor) {
    return MOTOR_STATUS_INVALID_ARGUMENT;
}

if (sample_count == 0U) {
    return SENSOR_STATUS_NO_DATA;
}

if (data_ready) {
    process_data();
}
```

不要写 `flag == true`；不要把普通整数当作布尔值使用。比较硬件状态码时保留其权威常量，而不是假设成功一定等于零。

### 7.4 循环、等待与无限循环

- 空循环体必须使用花括号和注释说明目的。
- 轮询硬件状态必须有超时或由硬件复位保证。
- 循环内部不额外修改循环控制变量。
- 主循环可以使用 `while (1)`；其他无限循环必须说明退出由复位、调度器或硬件事件控制。
- 大数组遍历和校验循环需要评估 ISR/实时路径最坏执行时间。

```c
deadline = system_time_ms() + UART_RESET_TIMEOUT_MS;
while (!uart_reset_done()) {
    if (time_reached(deadline)) {
        return UART_STATUS_TIMEOUT;
    }
}
```

上述阻塞形式仅适合初始化或明确允许阻塞的接口；运行阶段优先使用状态机。

### 7.5 统一清理路径

当函数按顺序获取多个资源时，可以使用向后的 `goto` 统一释放；标签描述需要执行的清理阶段。

```c
storage_status_t storage_write(storage_t *storage, const uint8_t *data, size_t length)
{
    storage_status_t status;

    status = storage_lock(storage);
    if (status != STORAGE_STATUS_OK) {
        return status;
    }

    status = storage_enable(storage);
    if (status != STORAGE_STATUS_OK) {
        goto unlock;
    }

    status = storage_transfer(storage, data, length);
    storage_disable(storage);

unlock:
    storage_unlock(storage);
    return status;
}
```

### 7.6 递归、VLA 和动态内存

- 递归默认 MUST NOT；只有栈深度有严格静态上限、经过分析且平台允许时才能例外。
- VLA MUST NOT；使用编译期数组、调用者缓冲区或固定对象池。
- 动态内存默认 MUST NOT 出现在实时控制、ISR 和长生命周期反复分配路径。
- ESP32/RTOS/网络栈确需动态内存时，按照架构规范记录分配阶段、内存区域、上限、失败路径和碎片验证。

### 7.7 副作用和可重入性

函数的隐藏副作用越少越好。读取接口不应同时清除状态，除非名称使用 `take`、`consume`、`read_and_clear` 等明确表达。回调注册、全局配置和硬件状态改变必须写入契约。

可能被多个任务、ISR 或回调并发调用的函数，需要标记为可重入、内部同步、调用者同步或禁止并发之一。

### 7.8 应避免的关键字

- `auto` 和 `register` MUST NOT 使用；它们在现代嵌入式 C 中没有可靠收益。
- `continue` SHOULD NOT 使用；只有它能减少嵌套并让循环主路径更清楚时才允许。
- `goto` SHOULD NOT 用于普通控制流；仅允许跳向同一函数后方的统一清理标签。
- 任何获准的 `continue` 或 `goto` 都必须保持局部、无循环跳转，并在代码审查中确认结构化替代方案不会更清晰。

```c
for (size_t index = 0U; index < sample_count; index++) {
    if (!sample_is_valid(&samples[index])) {
        /* 跳过无效输入可以避免包裹整个主处理路径。 */
        continue;
    }

    process_sample(&samples[index]);
}
```

## 8. 标准库与嵌入式限制

| 类别 | 默认策略 | 说明 |
| --- | --- | --- |
| `malloc`/`free` | 禁止 | 例外见架构规范的内存章节 |
| `printf`/格式化 | 限制 | 不在 ISR；评估代码体积、执行时间和缓冲区 |
| `strcpy`/`strcat`/`gets` | 禁止 | 使用带容量的实现或显式拷贝 |
| `memcpy`/`memset` | 允许 | 长度必须经过边界验证 |
| `assert` | 开发期允许 | 发布版本定义失败策略 |
| `rand` | 避免 | 需要随机性时使用硬件或经过说明的实现 |

所有库函数都应考虑栈、执行时间、重入性、线程安全和链接体积，不因“标准库”三个字自动视为适合 ISR 或实时路径。

### 8.1 字符串和格式化

- 使用 `snprintf`，容量来自真实数组的 `sizeof` 或接口参数。
- 检查返回值是否为负或大于等于容量，以识别编码失败和截断。
- 外部输入不保证以 `\0` 结尾；解析时始终携带长度。
- 不用 `strncpy` 假设结果一定终止；需要时显式写入末尾零字符。
- 二进制协议不使用字符串 API。

```c
int32_t written = snprintf(buffer,
                           sizeof(buffer),
                           "speed=%" PRId32 ",state=%u",
                           speed,
                           (unsigned int)state);
if ((written < 0) || ((size_t)written >= sizeof(buffer))) {
    return LOG_STATUS_TRUNCATED;
}
```

固定宽度整数格式优先使用 `<inttypes.h>` 中的 `PRIu32`、`PRId32` 等宏，避免假设 `uint32_t` 等同于某个平台的 `unsigned long`。

### 8.2 内存函数

`memcpy` 的源和目标不可重叠；可能重叠时使用 `memmove`。复制结构体前确认结构体不包含指针、填充敏感数据或硬件所有权。清零结构体只在全零确实代表合法初始状态时使用。

### 8.3 断言

断言用于发现程序员错误和内部不变量，不用于处理外部输入、通信错误或正常可恢复故障。项目必须定义发布版本的断言策略：保留并复位、记录后停机，或完全禁用；安全关键不变量不能只依赖可能被编译掉的断言。

### 8.4 编译器扩展

`__attribute__`、内联汇编、段属性、弱符号和编译器内建函数必须集中在平台边界或兼容宏中。公共领域接口不直接暴露编译器扩展。使用扩展时说明工具链、目标和无扩展时的替代行为。

### 8.5 数值转换和控制流库函数

- `atoi`、`atol`、`atof` MUST NOT 用于外部或持久化输入，因为它们不能可靠区分合法值、格式错误和范围溢出。
- 主机环境可使用 `strtol`、`strtoul`、`strtof` 等可报告解析位置和范围错误的函数，并检查结束指针及 `errno`。
- 资源受限固件可以使用项目内有长度参数、范围检查和明确状态码的解析器，避免为少量命令引入完整 C 运行库。
- 裸机和 RTOS 固件默认禁止 `abort`、`exit`、`_Exit`、`setjmp` 和 `longjmp`；系统停机、复位和故障恢复必须通过明确的平台或应用策略完成。
- 仿真程序、主机测试和第三方运行库确需上述函数时，可以作为构建目标级例外，不得让其语义进入固件公共 API。

```c
parse_status_t parse_u32(const char *text,
                         size_t text_length,
                         uint32_t minimum,
                         uint32_t maximum,
                         uint32_t *value);
```

## 9. 错误码与防御式编码

### 9.1 状态码

每个模块拥有自己的状态类型，或使用项目统一且语义稳定的公共状态类型。成功值固定且可读，错误值不通过“神秘负数”传播。

```c
typedef enum {
    UART_STATUS_OK = 0,
    UART_STATUS_INVALID_ARGUMENT,
    UART_STATUS_NOT_INITIALIZED,
    UART_STATUS_BUSY,
    UART_STATUS_TIMEOUT,
    UART_STATUS_IO_ERROR,
} uart_status_t;
```

不要假设不同枚举类型可以互换。跨层转换错误时显式映射，保留原始错误用于诊断。

### 9.2 边界防御

外部通信、DMA 长度、传感器异常值、持久化数据和用户输入属于不可信边界，必须完整校验。模块内部受控调用可以依赖契约，避免所有层重复同样检查。防御深度由数据来源、所有权和接口契约共同决定。

### 9.3 安全输出

PWM、速度、电流、角度和执行器命令在最终输出边界必须限幅；上层校验不能替代驱动边界保护。算术限幅需要先避免中间结果溢出。

```c
int32_t limited_output = clamp_i32(requested_output,
                                   MOTOR_OUTPUT_MIN,
                                   MOTOR_OUTPUT_MAX);
motor_apply_output(motor, (int16_t)limited_output);
```

## 10. 编译与静态检查

### 10.1 编译警告

项目代码 SHOULD 在可用工具链上启用高警告级别，例如 GCC/Clang 的 `-Wall -Wextra -Wconversion -Wshadow`。是否全局使用 `-Werror` 由第三方库和发布流程决定，但新增项目代码不得引入未解释警告。

每个抑制都应尽量局部，并说明为什么是误报或为什么当前设计安全。不得通过无意义类型转换、初始化或 `(void)` 大量吞掉真实问题。

### 10.2 静态断言和编译期检查

使用 `_Static_assert` 验证协议大小、数组关系、配置上限和类型假设：

```c
_Static_assert(MOTOR_COUNT <= 8U, "motor bitmap only supports eight instances");
_Static_assert(sizeof(uint32_t) == 4U, "platform requires 32-bit uint32_t");
```

### 10.3 静态分析

静态分析重点关注：越界、空指针、未初始化、整数溢出、死代码、资源泄漏、并发访问和未定义行为。工具报告的处理结果分为修复、带依据抑制或登记技术债务，不允许不经检查整体关闭规则集。

## 11. 工具和检查

推荐检查顺序：编译器高警告级别、clang-format、静态分析、单元测试和目标板冒烟测试。项目可以根据工具链调整具体命令，但不能降低“无新增警告、无未解释静态分析问题、无尾随空白”的基本要求。

个人 `.clang-format` 是格式化基线；自动格式化只在独立提交或功能提交的明确范围内运行，不混入无关代码重排。

## 12. 检查清单

- [ ] 公共符号有模块前缀，私有符号已使用 `static`。
- [ ] 列宽不超过 100，文件无 Tab 和尾随空白。
- [ ] 头文件自洽且没有泄露实现细节。
- [ ] 公共 API 说明所有权、返回值、阻塞性和并发限制。
- [ ] 指针、长度、索引、位移和类型转换经过检查。
- [ ] ISR 没有阻塞、动态内存、重量级格式化或复杂业务逻辑。
- [ ] 新增例外已记录原因、影响和验证方式。

## Changelog

### v1.2.0 — 2026-07-19

- 增加 C11/gnu11 语言基线和平台扩展隔离规则。
- 增加保留标识符、标准库名称和第三方前缀保护规则。
- 增加 UTF-8、LF、不可打印字符和仓库行尾检查规则。
- 增加魔法数字、函数式宏和关键字限制规则。
- 增加数值解析以及固件控制流库函数限制。
- 修正公共 API 文档示例中的状态码名称。
- 将规范正文中的历史对比表述改为当前规则定义。

### v1.1.0 — 2026-07-19

- 恢复并扩展命名、格式化、头文件、注释、类型、函数和标准库的详细规则。
- 增加完整示例、接口边界、浮点/定点、原子性、动态内存例外和错误码规范。
- 增加编译警告、静态断言、静态分析以及外部数据边界要求。
- 明确 BARR-C:2018 与 Linux 内核编码规范仅作为参考来源，不声明完全兼容。
- 修正 v1.0.0 过度概括、无法作为日常查阅规范的问题。

### v1.0.0 — 2026-07-18

- 从原 `CODING_STANDARD.md` 拆出嵌入式 C 代码风格内容。
- 删除 Python 细则，不覆盖 C++。
- 将默认列宽调整为 100 字符。
- 增加规则等级、稳定章节、例外记录和持续维护机制。
