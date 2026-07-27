# 嵌入式 C 代码风格规范

> 版本：v1.1.1<br>
> 创建日期：2026-07-18<br>
> 发布日期：2026-07-24<br>
> 最后更新：2026-07-24<br>
> 状态：Active<br>
> 适用范围：个人 STM32、TI MSPM0、ESP32 及其他 MCU 的裸机/RTOS C 固件

本文档规定运行在 MCU 上的嵌入式 C 固件如何命名、排版、注释和使用 C 语言。软件分层、模块边界、状态机、通信架构、裸机/RTOS 运行模型等内容见
[ARCHITECTURE_STANDARD.md](ARCHITECTURE_STANDARD.md)；Git、提交、测试和发布流程见
[REPOSITORY_WORKFLOW.md](REPOSITORY_WORKFLOW.md)。本文档不覆盖 C++、PC 主机程序、POSIX 软件、嵌入式 Linux 用户态或 Linux 内核源码；Python 遵循 PEP 8，不在本文档维护 Python 细则。

本规范直接参考 [BARR-C:2018](https://barrgroup.com/embedded-systems/books/embedded-c-coding-standard1)、[Linux 内核编码风格](https://kernel.org/doc/html/next/process/coding-style.html)中的通用 C 表达习惯、[ISO C/WG14](https://www9.open-std.org/JTC1/SC22/WG14/) 语言规则和个人嵌入式开发实践。存在冲突时，以本文档的明确规则为准；本文档不声明完全兼容任何外部规范。

## 1. 使用方式

### 1.1 规则等级、ID 与符合性

- **MUST**：默认必须遵守；违反时应在提交前修复。
- **SHOULD**：通常应遵守；有明确技术原因时可以例外。
- **MAY**：可选实践，由项目规模和工具链决定。
- **EXCEPTION**：经过记录、评估和验证后允许的例外。

正文中的“必须、不得、禁止”与 MUST 等价，“通常应、推荐、优先”与 SHOULD 等价，“可以、可选”与 MAY 等价。带规则 ID 的加粗条款是稳定的核心规则，其后直到下一个规则 ID 或同级标题之前的正文、表格和细则用于具体化该规则。解释和代码示例本身不构成独立规则。

规范性规则使用 `STYLE-<CATEGORY>-NNN` 形式的稳定 ID：

| 类别 | 主题 | 类别 | 主题 |
| --- | --- | --- | --- |
| `GOV` | 治理、适用性和例外 | `LANG` | C 基线和工具链扩展 |
| `NAME` | 命名和标识符 | `FMT` | 格式和文件编码 |
| `DOC` | 注释和接口文档 | `HDR` | 头文件、链接和可见性 |
| `TYPE` | 类型、对象和数据布局 | `EXPR` | 表达式、转换和算术 |
| `CTRL` | 函数和控制流 | `LIB` | freestanding C 库使用 |
| `ERR` | 状态、边界和防御 | `TOOL` | 编译、格式化和静态检查 |

**STYLE-GOV-001 [MUST] — 声明符合本规范的固件必须记录规范版本、所选 C 标准、编译器与 ABI、目标 MCU、clang-format 主版本、规则例外和验证结果。**

符合性按以下方式判定：

- MUST 全部满足，或存在仍在有效期内且经过验证的例外。
- SHOULD 已采用，或记录未采用的技术原因和影响。
- MAY 由项目选择，不影响符合性。
- 未记录的偏离视为不符合。

**STYLE-GOV-002 [MUST] — 规则例外必须引用真实规则 ID，并记录原因、影响范围、替代措施、验证方式、责任人和复审/失效条件。**

规则移动章节时保留 ID，废弃规则的 ID 不再复用。硬件额定值、C 语言约束、工具链 ABI 和适用的法律/安全要求不能由本文档的例外流程豁免。

**STYLE-GOV-003 [MUST] — 芯片厂商生成代码、CMSIS、HAL 和第三方库必须保持其权威风格与可更新边界；项目手写的适配器、BSP、Driver 和上层固件遵循本文档。**

不得为了套用本文档直接格式化或修改外部代码。Linux 内核源码遵循对应内核规范；未来的嵌入式 Linux 用户态代码在实际采用该平台时建立独立规范或 Profile。

### 1.2 可维护性原则

1. 代码首先服务于阅读、调试和验证，其次才是减少行数。
2. 同一仓库只采用一种写法；尚未完成迁移的范围必须登记例外，不形成两套长期并存的规则。
3. 规则尽量由编译器、clang-format、静态分析或 CI 检查执行；工具无法判断的规则必须通过代码审查确认。
4. 任何例外都写明原因、影响范围和失效条件，不在代码中留下无法解释的特殊写法。

### 1.3 维护与版本

本文档采用 SemVer：

- `MAJOR`：规则体系或核心风格发生不兼容变化。
- `MINOR`：增加规则或工具支持，但不改变已有规则含义。
- `PATCH`：修正文字、示例、链接或排版。

新增规则放入已有主题章节；只有主题确实独立时才新增章节。废弃规则先标记为 `Deprecated`，并在变更记录中给出替代规则和计划移除版本。

### 1.4 C 语言基线

**STYLE-LANG-001 [MUST] — 项目手写固件必须以 ISO C11 或更新标准编译，并在构建系统中显式选择实际标准。**

- C11 是个人 MCU 固件的最低默认基线。
- 工具链、厂商 SDK、静态分析和全部构建目标支持时，可以选择 C17 或 C23。
- 仅支持 C99 的旧工具链必须登记 `STYLE-LANG-001` 例外，记录缺失能力、影响和替换计划。
- `.c` 文件由工具链的 C 前端编译，禁止使用 C++ 前端解释 C 源码。
- 可复用组件声明最低 C 标准，不为局部便利无依据地提高调用项目的语言要求。

**STYLE-LANG-002 [MUST] — GNU 扩展、编译器属性、`#pragma`、内联汇编和专有关键字必须限制在确有硬件或工具链依据的平台边界。**

使用 GNU 模式时，标准版本与项目基线对应，例如 `gnu11` 或 `gnu17`。扩展优先封装在 HAL、BSP、Driver 或平台兼容宏中，并说明支持的编译器、目标、语义和无扩展时的处理方式。不得用 `#define` 重命名 C 关键字、隐藏控制流或构造项目私有伪语言。

安全关键或需要认证的项目不能仅依赖本文档；应根据产品风险采用适用的行业安全规范、静态分析、可追踪需求和正式偏离审批。本文档不宣称其规则来源于或实现了 MISRA C、CERT C 等合规体系。

## 2. 命名

### 2.1 基本形式

**STYLE-NAME-001 [MUST] — 项目手写标识符必须遵守统一命名形式；所有外部链接符号和公共类型必须带有稳定且无冲突的模块前缀。**

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
    MOTOR_STATE_IDLE,    /* 电机已就绪，当前无运动指令 */
    MOTOR_STATE_RUNNING, /* 电机正在执行运动指令 */
    MOTOR_STATE_FAULT,   /* 电机故障，需要复位或恢复 */
} motor_state_t;

motor_status_t motor_set_speed(motor_handle_t *motor, int16_t speed_rpm);
```

### 2.2 词汇与缩写

**STYLE-NAME-002 [MUST] — 名称必须表达对象、动作、单位和布尔语义，并在同一项目中保持词汇与缩写唯一。**

- 禁止在同一项目中混用 `timeout`/`time_out`、`position`/`pos` 等同义写法。
- 允许使用 `rx`、`tx`、`id`、`cfg`、`ctx`、`buf`、`len`、`cnt`、`idx`、`err` 等行业通用缩写。
- `position` 默认不缩写为 `pos`；`tmp` 只用于生命周期极短且语义确实为临时中转的局部变量。
- 新缩写首次出现时在类型、注释或模块说明中解释；不要为了缩短名称删除关键语义。
- 时间、长度、角度、频率等单位写入名称：`timeout_ms`、`sample_hz`、`angle_deg`、`buffer_size`。
- 布尔变量使用 `is_`、`has_`、`can_` 或 `should_` 等能表达真假语义的前缀。

### 2.3 文件名和头文件保护

**STYLE-NAME-003 [MUST] — 手写源文件必须使用全小写 `snake_case` 名称；头文件保护符必须由项目、路径和文件名形成全仓唯一标识。**

文件使用全小写 `snake_case.c`、`snake_case.h`；一对接口和实现文件名称必须完全相同，不使用空格、连接符或仅大小写不同的名称。

```c
#ifndef PROJECT_CONTROL_MOTOR_CONTROLLER_H
#define PROJECT_CONTROL_MOTOR_CONTROLLER_H /* 头文件保护 */

/* 接口内容 */

#endif /* PROJECT_CONTROL_MOTOR_CONTROLLER_H */
```

保护符中的 `PROJECT_CONTROL` 表示项目名和公共路径，实际项目替换为自己的稳定前缀。默认使用标准头文件保护，不使用 `#pragma once`。如果工具链或生成器强制使用其他形式，应在项目 README 或例外记录中说明。

### 2.4 宏、常量和配置项命名

**STYLE-NAME-004 [MUST] — 宏、编译期开关和具名常量必须通过前缀、后缀和单位表达作用域与语义。**

宏和编译期开关使用 `UPPER_SNAKE_CASE`。功能开关采用肯定语义，值必须明确为 `0` 或 `1`，不要通过“是否定义”表达同一项目中的普通配置。

```c
#define CONFIG_MOTOR_USE_CURRENT_LIMIT 1     /* 启用电机电流限制 */
#define MOTOR_CONTROL_PERIOD_MS        5U    /* 电机控制周期，单位：ms */
#define GYRO_MAX_DELTA_DEG             50.0F /* 陀螺仪单次允许的最大角度变化，单位：度 */
```

规则：

- 公共宏带模块前缀；仅在一个 `.c` 使用的宏仍放在该文件内。
- 表示数量的名称使用 `_COUNT`，容量使用 `_CAPACITY`，字节长度使用 `_SIZE_BYTES`。
- 位掩码以 `_MASK` 结尾，位编号以 `_BIT` 结尾。
- 超时必须带单位，如 `_TIMEOUT_MS`；不得使用含义不清的 `_TIME`。
- 不把普通变量伪装成全大写“常量”；只读运行时对象使用 `const` 和普通变量命名。
- 不在公共头文件定义无模块前缀的 `MIN`、`MAX`、`BIT` 等通用宏。

### 2.5 函数、回调和入口命名

**STYLE-NAME-005 [MUST] — 函数名必须从调用者视角表达动作，并通过名称区分阻塞、非阻塞推进、查询、普通回调和 ISR 入口。**

函数名以动词开头，并让调用者能从名称判断动作和阻塞语义：

| 类别 | 推荐形式 | 示例 |
| --- | --- | --- |
| 生命周期 | `<module>_init/deinit/start/stop/reset` | `imu_start()` |
| 配置 | `<module>_configure/set/get` | `motor_set_limit()` |
| 周期入口 | `<module>_task` | `control_task()` |
| 单步处理 | `<module>_process/poll` | `protocol_process()` |
| 查询 | `<module>_is/has/can` | `uart_is_busy()` |
| ISR 入口 | `<module>_on_<event>_isr` | `uart_on_rx_isr()` |
| 普通回调 | `<module>_on_<event>` | `button_on_pressed()` |
| 转换 | `<source>_to_<target>` | `ticks_to_ms()` |

`task` 只用于项目已经定义的周期入口；`process` 表示处理一次已有输入；`poll` 表示主动检查一次状态。可能阻塞的函数使用 `wait`、`read_blocking` 等明确名称，不让普通 `read`、`write` 隐藏阻塞语义。

### 2.6 类型和实例命名

**STYLE-NAME-006 [MUST] — 类型名必须表达抽象角色，实例名必须表达具体用途，不使用类型编码前缀代替 C 类型系统。**

- 配置类型使用 `<module>_config_t`，实例/句柄使用 `<module>_handle_t` 或 `<module>_t`。
- 状态、事件、命令、错误分别使用 `_state_t`、`_event_t`、`_command_t`、`_status_t`。
- 类型名称表达抽象含义，不包含无必要的平台名称；平台适配类型可使用 `stm32_`、`mspm0_` 等前缀。
- 不额外给全局变量加 `g_`，而是通过 `static`、模块 API 和作用域控制可见性。
- 单字母变量仅用于极短循环索引或数学公式；生命周期超过几行时使用语义名称。

### 2.7 保留标识符与名称冲突

**STYLE-NAME-007 [MUST] — 项目标识符不得进入 ISO C、编译器、厂商 SDK、RTOS 或第三方依赖保留的命名空间。**

- 项目标识符不得包含双下划线，不得以双下划线开头，也不得以下划线后接大写字母开头。
- 文件作用域的项目标识符不得以下划线开头；为保持一致，其他作用域也不自行创建下划线前缀名称。
- 不定义与 C 标准库函数、宏、类型或全局对象重名的符号，例如 `strlen`、`errno`、`assert`。
- 不复用编译器、CMSIS、HAL、RTOS、芯片厂商和第三方库保留的前缀。
- 中断入口、启动符号和生成器要求的名称属于平台例外，必须保持其权威拼写并隔离在平台层。
- 新模块选择公共前缀前，应搜索仓库和依赖，确认不存在同名公共符号。

## 3. 格式化

### 3.1 缩进和括号

**STYLE-FMT-001 [MUST] — 手写 C 代码必须使用 4 个空格、Linux 花括号布局、完整控制流花括号和一行一条语句。**

- 缩进 4 个空格，禁止 Tab。
- 控制流使用 K&R 风格；函数定义使用函数名单独一行的风格。
- 所有控制流都必须使用花括号，即使只有一条语句。
- 一条语句独占一行。

```c
if (motor_is_ready(motor)) {
    motor_start(motor);
}

/** @brief 推进电机周期任务 */
void motor_task(void)
{
    motor_update();
}
```

### 3.2 列宽和续行

**STYLE-FMT-002 [MUST] — 普通代码、宏和行尾注释必须控制在 100 列内，并按语义边界稳定续行。**

普通代码列宽上限为 **100 个字符**；超过上限必须在语义自然的位置换行。宏定义的代码和行尾注释也应尽量控制在 100 列以内，不能以列宽为理由截断标识符。

- 函数参数过多时，每个参数单独一行或按工具格式化结果排列。
- 长条件在低优先级逻辑运算符后换行。
- 长算术表达式在运算符后换行。
- 三元表达式的 `?`、`:` 分行时保持视觉层次一致。
- 长字符串使用相邻字符串拼接，不依赖超长单行。
- 函数参数和调用实参的续行固定缩进 4 个空格，不按左括号位置对齐。
- 长算术和逻辑表达式的操作数使用视觉对齐，保持运算层次清晰。

```c
if ((sensor_value > threshold_high) &&
    (system_state == SYSTEM_STATE_ACTIVE) &&
    safety_check_passed()) {
    control_enable();
}
```

### 3.3 空格、空行和对齐

**STYLE-FMT-003 [MUST] — 空格、空行和局部对齐必须由语法与逻辑分组决定，不得依赖 Tab、尾随空白或跨无关语句的装饰性对齐。**

- 二元运算符、赋值运算符和逗号后使用空格。
- 函数名与左括号之间不加空格；控制关键字与左括号之间加空格。
- 一元运算符、数组下标、`.` 和 `->` 两侧不加空格。
- 函数之间使用一个空行；逻辑段落之间最多使用一个空行。
- 文件末尾必须且只能有一个换行符，禁止行尾空白。
- 连续声明或赋值只有在确实属于同一组时才手动对齐，不为了对齐而破坏自动格式化。

### 3.4 `switch` 和条件表达式

**STYLE-FMT-004 [MUST] — `switch` 的每个分支必须显式终止或标记贯穿，条件表达式不得隐藏赋值和不可见副作用。**

- `case` 缩进一层，语句再缩进一层。
- `default` 必须存在，除非编译器或协议定义已证明枚举值完整且代码中有明确说明。
- 每个 `case` 必须以 `break`、`return`、同函数内的清理 `goto` 或明确标记的贯穿结束；修改状态变量不能代替控制流终止。
- `default` 放在最后；`case` 内需要局部声明时，使用花括号建立独立作用域。
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

**STYLE-FMT-005 [MUST] — 每个声明只引入一个对象，变量在最小作用域内获得有意义的初始值，聚合初始化使用指定成员和尾逗号。**

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
motor_status_t motor_configure(
    motor_handle_t *motor,
    const motor_config_t *config,
    const motor_port_t *port);

status = protocol_decode_frame(
    &decoder,
    rx_buffer,
    rx_length,
    &decoded_frame);
```

长表达式的换行应让操作顺序清楚：

```c
uint32_t compensated_value = base_value +
                             calculate_offset(&calibration) +
                             (temperature_delta * compensation_factor);
```

### 3.7 预处理指令

**STYLE-FMT-006 [MUST] — 预处理指令必须保持可搜索的顶格 `#` 和有限嵌套，条件内的 C 代码只按其 C 作用域缩进。**

预处理指令的 `#` 顶格；嵌套指令由 clang-format 在 `#` 后缩进。条件内 C 代码保持函数、块或文件作用域本身的缩进，不因 `#if` 额外增加层级。每个 `#else`、`#elif` 和较远的 `#endif` 注明对应条件。条件编译集中在平台适配边界，嵌套不超过两层。

```c
/** @brief 启动构建配置选定的 UART 接收方式 */
static void uart_start_configured(void)
{
#if CONFIG_UART_USE_DMA
    uart_dma_start(&uart);
#else /* CONFIG_UART_USE_DMA */
    uart_interrupt_start(&uart);
#endif /* CONFIG_UART_USE_DMA */
}
```

不要用条件编译复制大段业务逻辑；应选择不同适配器实现，再由统一接口调用。

### 3.8 格式化禁用区

**STYLE-FMT-007 [MUST] — `clang-format off/on` 只能保护自动格式化会破坏语义对应关系的最小连续区域，并必须注明原因。**

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

**STYLE-FMT-008 [MUST] — 手写源码必须使用无 BOM 的 UTF-8、LF 行尾、单个文件末尾换行，并且不得含有尾随空白或非预期控制字符。**

- 所有手写源码和文本配置使用 UTF-8；默认不带 BOM。
- 行尾必须使用 LF（ASCII `0x0A`），禁止把 CRLF 混入仓库。
- 除换行外，源码不得包含控制字符或不可打印字符；字符串中的 Tab 使用转义序列 `\t`。
- 文件末尾有且仅有一个 LF，禁止尾随空白和多余空行。
- 生成代码无法满足上述规则时，不直接修改生成文件；在 `.gitattributes`、生成步骤或检查范围中记录例外。

推荐在仓库根目录使用 `.editorconfig` 和 `.gitattributes` 固化 UTF-8、LF、缩进及文件末尾换行，CI 使用 `git diff --check` 检测尾随空白。

## 4. 注释与接口文档

### 4.1 注释原则

**STYLE-DOC-001 [MUST] — 注释必须提供代码本身无法可靠表达的原因、硬件约束、时序、单位、所有权或异常条件，并与实现同步维护。**

注释默认使用中文，说明“为什么”、硬件限制、时序要求、单位、所有权和异常条件。不要用注释复述明显的代码行为，也不要在注释中向 AI 或其他人发送临时讨论。

### 4.2 函数注释（仅 `.c` 文件）

**STYLE-DOC-002 [MUST] — 每个手写 `.c` 函数定义必须具有符合 Doxygen 语义的函数注释；`.h` 文件不得包含 Doxygen 函数文档。**

每个手写函数定义前必须写 Doxygen 函数注释，包括公共函数、`static` 私有函数、回调函数和 ISR。
函数文档注释只写在 `.c` 文件的函数定义前；`.h` 文件禁止写 `@brief`、`@param`、`@return`、`@retval`
形式的 Doxygen 函数注释。

```c
/**
 * @brief  根据当前位置计算电机的目标速度
 * @param  motor    电机控制器指针
 * @param  target   目标位置（编码器计数值）
 */
void motor_move_to(motor_controller_t *motor, int32_t target)
{
    /* 实现 */
}
```

注释字段遵循以下规则：

- `@brief` 必填。
- 每个参数对应一个 `@param`；没有参数时省略 `@param`。
- `void` 函数不写返回项。
- 返回连续数值、对象、指针或统一描述即可覆盖的结果时，使用 `@return` 说明返回语义。
- 返回有限状态码时，使用一个或多个 `@retval VALUE 描述` 列出调用者需要处理的结果。
- 硬件依赖、阻塞性、单位、取值范围、所有权和并发限制使用 `@note` 或 `@warning` 按需说明。
- 确实无参数且无返回值的简单函数可写为 `/** @brief 功能说明 */`。

`@return` 和 `@retval` 的语义遵循 [Doxygen Special Commands](https://www.doxygen.nl/manual/commands.html)。不得使用 `@retval 无` 或省略具体返回值名称的 `@retval`。

### 4.3 宏和枚举成员注释

**STYLE-DOC-003 [MUST] — 每个手写宏定义和枚举成员必须分别具有对应的中文普通注释。**

每个宏定义和每个枚举成员都必须分别添加对应的中文注释。类型、枚举或宏组上方的总体注释不能代替逐项注释。注释应说明用途、单位、硬件含义、协议含义或特殊限制，不只是重复名称。

宏定义或枚举成员和行尾注释的总长度不得超过 100 列；超过时将对应注释放在该项上方。

```c
#define MAX_PWM_VALUE  2000U /* PWM 最大占空比值 */
#define PID_KP_DEFAULT 1.5F /* PID 比例系数默认值 */

typedef enum {
    MOTOR_MODE_STOP,     /* 停止输出并保持安全状态 */
    MOTOR_MODE_SPEED,    /* 按目标速度进行闭环控制 */
    MOTOR_MODE_POSITION, /* 按目标位置进行闭环控制 */
} motor_mode_t;
```

### 4.4 文件头注释

**STYLE-DOC-004 [MUST] — 每个手写 `.c` 文件必须用 `@file` 和 `@brief` 说明身份与职责；作者、日期和版本由 Git 与发布系统管理。**

仅 `.c` 文件开头写详细的 Doxygen 文件头，`.h` 文件禁止写文件头注释。

```c
/**
 * @file    motor_controller.c
 * @brief   步进电机运动控制模块
 * @note    依赖 PWM 定时器已初始化
 * @warning 未做死区保护，PWM 互补输出需外部电路保证
 */
```

`@file` 和 `@brief` 必填，`@note` 和 `@warning` 按需填写。源码文件不维护易失的作者、日期和独立文件版本字段；许可证确有要求时保留权威版权头。

### 4.5 文件和代码块

大型 `.c` 文件可以按“私有类型、私有变量、私有函数、公共 API、回调”分组；小文件不强制添加分隔线。分隔线使用
`/* ===== 分组名 ===== */` 形式。

### 4.6 注释位置与行尾注释

普通注释独占一行并写在被解释代码上方，不放在普通语句右侧。宏定义、极短的变量用途说明、枚举值、寄存器位和紧凑数据表允许使用行尾注释。

```c
/* 编码器安装方向相反，因此在进入控制算法前统一校正符号。 */
position = -position;

#define MOTOR_PWM_MAX 2000U /* 20 kHz 定时器周期对应的最大比较值 */
```

### 4.7 TODO 与临时标记

统一使用以下标签：

- `TODO(owner/date):` 尚未实现但不影响当前正确性。
- `FIXME(owner/date):` 已知缺陷，需要修复。
- `HACK(owner/date):` 有验证依据的临时绕过方案。
- `NOTE:` 容易被误改的重要事实。

标签必须说明触发条件或完成标准，不写“以后优化”之类不可执行文本。长期设计决定应进入文档或 ADR，不依赖 TODO 保存。

### 4.8 注释的错误用法

- 不保留大段被注释掉的旧代码，版本历史由 Git 保存。
- 不记录已经失效的历史讨论和调试输出。
- 不用注释掩盖糟糕命名。
- 不承诺代码没有实现的行为。
- 修改接口或时序时同步更新注释；错误注释比没有注释更危险。

## 5. 头文件与源文件

### 5.1 最小包含原则

**STYLE-HDR-001 [MUST] — 每个公共头文件必须自洽并只包含其声明直接需要的权威类型；每个 `.c` 文件必须首先包含自己的头文件。**

- 头文件只包含其公共声明直接需要的类型。
- 能用前向声明时，不为实现细节引入完整头文件。
- `.c` 文件第一个 include 必须是本模块头文件，用于尽早发现头文件自洽性问题。
- 其余 include 按“项目接口、项目其他模块、第三方、标准库、平台/HAL”组织；项目可通过 clang-format 或 lint 保持稳定。

### 5.2 接口暴露

**STYLE-HDR-002 [MUST] — 公共头文件只暴露调用者所需的类型与能力，模块实现状态和辅助符号必须保留在 `.c` 私有作用域。**

头文件只放公共类型、宏、枚举、常量、必要的 `extern` 声明和函数声明。确有类型安全、地址语义或经过测量的性能需求时，可以在头文件定义小型 `static inline` 函数；该函数必须短小、行为直接且不包含复杂控制流。公共头文件不定义具有外部链接的普通 `inline` 函数，避免依赖编译器不同的内联与外部定义语义。

实现变量、私有类型和内部辅助函数放在 `.c` 中，并使用 `static` 限制作用域。不要为了方便调试把整个内部结构体暴露给上层。

头文件不写文件头和 Doxygen 函数文档。宏和枚举成员必须使用第 4.3 节规定的逐项中文普通注释；常量和类型按需使用简短的中文普通注释。函数声明只按接口类别使用普通中文分组注释，不逐个重复 `.c` 文件中的函数文档：

```c
/* 生命周期接口 */
motor_status_t motor_init(motor_handle_t *motor, const motor_config_t *config);
motor_status_t motor_deinit(motor_handle_t *motor);

/* 运行与控制接口 */
motor_status_t motor_start(motor_handle_t *motor);
motor_status_t motor_stop(motor_handle_t *motor);

/* 查询接口 */
motor_status_t motor_get_state(const motor_handle_t *motor, motor_state_t *state);
```

接口按实际能力从“类型、配置、生命周期、运行与控制、查询、处理入口、ISR 入口”等类别中选择，不创建空分组。

### 5.3 `const`、`volatile` 和参数方向

**STYLE-HDR-003 [MUST] — 接口中的可修改性和参数方向必须通过类型限定、参数排列和必要注释明确表达。**

- 对象或视图在当前作用域内不应被修改时使用 `const`；不修改指向内容的输入指针必须限定为 `const`。
- MMIO 或平台契约要求编译器每次真实访问的异步对象使用 `volatile`，并在注释中说明访问者与同步方式。
- `volatile` 不保证复合操作的原子性，也不替代锁、临界区或内存屏障。
- 缓冲区参数必须同时提供可验证的长度或容量；输出参数的有效条件按第 7.1 节说明。

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

各类 include 按上述顺序连续排列，类别之间不留空行。同一类内的顺序由项目统一；个人配置默认关闭 clang-format 自动排序，因为工具无法可靠区分自身、项目、第三方和平台头文件。

### 5.5 头文件自洽与依赖泄漏

**STYLE-HDR-004 [MUST] — 公共头文件必须能够被单独包含并通过编译，不得依赖包含顺序、聚合项目头或未声明的平台类型。**

每个公共头文件必须能够被单独 include 并通过编译。头文件中出现某个类型时，要么包含该类型的权威头文件，要么使用合法的前向声明。禁止依赖“调用者碰巧先 include 了另一个文件”。

公共头文件不得包含 `main.h`、项目总头文件或仅为了获得一个平台宏而包含巨大的 HAL 聚合头。确实需要平台类型时，应考虑不透明句柄、端口接口或小型平台类型头。

### 5.6 全局变量与 `extern`

**STYLE-HDR-005 [MUST] — 模块状态默认使用私有 `static` 对象和公共 API；外部链接对象必须具有唯一声明、唯一定义和明确所有者。**

默认使用模块内部 `static` 状态和 API。`extern` 只用于工具链要求、启动文件、平台生成句柄或经过说明的极少数共享对象；声明只放在一个权威头文件，定义只存在一处。

不得在多个头文件重复声明同一个全局对象，也不得通过公共头文件暴露可由任意代码写入的大型状态结构体。

## 6. 类型、数据和表达式

### 6.1 固定宽度和布尔值

**STYLE-TYPE-001 [MUST] — 数据类型必须依据数值范围、硬件接口、C 库签名和目标数据模型选择，不得假设 `int`、`long`、指针或 C 字节在所有 MCU 上具有相同宽度。**

硬件寄存器、通信协议、持久化格式、确定范围的数值和跨平台数据必须使用 `<stdint.h>` 的固定宽度类型。缓冲区容量、对象大小和标准库索引使用 `size_t`；指针差值使用 `ptrdiff_t`；平台、标准库和第三方 API 保留其权威类型。普通 `int`、`long` 和 `short` 不用于需要确定宽度的数据。

布尔语义使用 `<stdbool.h>` 的 `bool`。只有在明确表达字符、字符串或标准库接口时使用 `char`。对象地址需要整数表示时只在平台边界使用 `uintptr_t`/`intptr_t`，并验证实现提供该类型及转换语义。

C 的一个字节由 `CHAR_BIT` 定义，不天然等于 8 bit。通信协议、Flash 格式或设备寄存器以八位 octet 为基础时，项目必须验证该假设：

```c
#include <limits.h>

_Static_assert(CHAR_BIT == 8, "firmware protocol requires 8-bit bytes");
```

### 6.2 枚举、宏和常量

**STYLE-TYPE-002 [MUST] — 枚举只表达 C 代码中的离散语义；寄存器位、位掩码、协议字段、持久化字段和 ABI 数据必须使用明确宽度的整数表示。**

状态、命令、事件和错误等离散的具名集合使用枚举。容量、位掩码、寄存器位、浮点常量、运算表达式和配置值根据类型与作用域选择 `static const`、宏或配置对象，不为了减少宏数量强行放入枚举。

枚举的底层整数类型和对象大小由实现决定，不直接 `memcpy` 或强制转换枚举对象形成线协议。跨边界时先验证枚举值，再显式编码为规定宽度的整数。位掩码使用无符号宏或权威寄存器定义；需要精确常量类型时使用 `UINT32_C()` 等 `<stdint.h>` 常量宏。

每个宏和枚举成员都必须按第 4.3 节逐项注释。宏参数和整体表达式必须加括号，多语句宏使用 `do { ... } while (0)`。

### 6.3 结构体和协议数据

**STYLE-TYPE-003 [MUST] — C 结构体布局不得直接充当通信、持久化或跨工具链 ABI；硬件映射必须采用厂商权威定义或经过记录的编译器/ABI 契约。**

硬件寄存器映射、通信帧和持久化数据必须显式考虑填充、对齐、大小端和版本兼容。协议与持久化数据使用逐字段序列化/反序列化，不依赖编译器默认布局。

C bit-field 的分配顺序、容器和对齐具有实现相关性，不用于协议、Flash 布局或项目自定义的可移植寄存器映射。MMIO bit-field 只使用芯片厂商提供的权威定义，或限制在已经验证并记录编译器版本、ABI、目标和生成代码检查的私有平台实现中；其他寄存器操作使用明确宽度的掩码和移位。

### 6.4 初始化和表达式安全

**STYLE-EXPR-001 [MUST] — 表达式必须避免未定义行为、未验证的窄化、意外整数提升、非法移位和不可判定的求值副作用。**

- 局部变量在使用前初始化；不要用无意义初始化掩盖控制流问题。
- `sizeof` 优先对变量或成员使用；求数组元素数量时必须确认操作数是真实数组而不是已经退化的指针。
- 有符号整数运算必须在执行前保证结果可表示；不得依赖有符号溢出。
- 无符号回绕只用于经过说明的模运算、计数器或 deadline 语义，普通范围计算必须预先检查。
- 有符号和无符号值混合运算前统一到能够表达双方范围的类型，不依赖通常算术转换猜测结果。
- 缩窄、符号变化和浮点/整数转换必须先验证源值范围，再在明确边界进行强制转换。
- 位操作使用无符号操作数；移位量必须非负且小于提升后类型的位宽，左移结果必须可表示。
- 一个完整表达式内不多次修改同一对象，不依赖函数实参或子表达式之间未规定的求值顺序。
- 实现定义行为只允许出现在记录了编译器、ABI 和验证方式的平台边界，不进入协议和通用算法契约。

### 6.5 浮点与定点

**STYLE-EXPR-002 [MUST] — 浮点或定点表示必须具有明确的范围、精度、异常值策略和目标 MCU 执行成本。**

浮点使用由 MCU、FPU、实时预算和数值需求共同决定。规则如下：

- 浮点字面量带 `F` 后缀，避免无意使用 `double`：`0.5F`。
- 无 FPU 或硬实时路径应测量浮点代价，必要时使用定点数。
- 定点类型在名称中标明比例或通过类型文档说明 Q 格式。
- 经过计算的连续量按误差模型使用容差比较；只从相同精确常量赋值且无算术过程的离散值可以精确比较。
- 传感器、协议或计算结果可能产生 NaN/Inf 时，在进入状态判断和控制输出前使用 `isfinite()` 或等价检查拒绝异常值。
- ISR 中只有在确认 FPU 上下文和最坏时间后才允许浮点运算。

```c
if (fabsf(measured_angle - target_angle) <= ANGLE_EPSILON_DEG) {
    state = CONTROL_STATE_SETTLED;
}
```

### 6.6 联合体和判别字段

**STYLE-TYPE-004 [MUST] — 联合体必须具有权威判别字段，并且只能读取判别字段当前指定的有效成员。**

联合体必须有明确的判别字段，读取前根据判别值确认当前有效成员。禁止由调用者凭隐式约定猜测有效成员。

```c
typedef enum {
    FRAME_PAYLOAD_COMMAND,   /* 当前有效成员为控制命令 */
    FRAME_PAYLOAD_TELEMETRY, /* 当前有效成员为遥测数据 */
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

**STYLE-TYPE-005 [MUST] — packed、未对齐数据和外部字节流必须通过明确的字节序与对齐处理访问，不得直接解引用可能未对齐的多字节成员。**

协议默认采用逐字段序列化和解析，显式处理边界、字节序和未对齐访问。packed 结构体仅用于硬件描述或工具链 ABI 明确要求的场景，并用静态断言验证大小和关键偏移。访问 packed 多字节成员时，先通过逐字节解码或 `memcpy` 转入满足对齐要求的普通对象，再处理字节序。

```c
_Static_assert(sizeof(protocol_header_t) == PROTOCOL_HEADER_SIZE,
               "protocol header layout changed");
```

### 6.8 `volatile`、原子与内存可见性

**STYLE-TYPE-006 [MUST] — MMIO、ISR、DMA 和 RTOS 共享数据必须分别采用目标平台要求的访问限定、所有权、原子操作、临界区、屏障和 cache 维护。**

```c
/* ISR 写、主循环读；目标平台已验证对齐 bool 读写的原子性。 */
static volatile bool dma_done;
```

- MMIO 使用厂商头文件或平台层提供的 `volatile` 寄存器定义，不复制一套未经验证的限定规则。
- ISR 异步修改且任务直接观察的简单对象可以使用 `volatile`，但必须另行证明访问宽度的原子性和单写者关系。
- 读-改-写、多个字段的一致快照和多个写者访问使用临界区、目标原子指令或 RTOS 同步原语。
- DMA 缓冲区不因“被 DMA 使用”就整体声明为 `volatile`；其正确性来自所有权交接、完成事件、内存区域、屏障和 cache clean/invalidate。
- `<stdatomic.h>` 只有在编译器、运行库和目标 MCU 已验证支持，并且所用操作满足锁自由性、ISR 可用性和实时预算时采用；否则使用明确的平台原语。
- 编译器屏障、CPU 内存屏障和 cache 维护解决不同问题，平台封装必须选用与硬件手册一致的操作。

### 6.9 魔法数字与具名常量

**STYLE-EXPR-003 [MUST] — 具有硬件、协议、时序、容量或产品语义的字面量必须由名称、类型和单位表达。**

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

**STYLE-EXPR-004 [MUST] — 函数式宏不得产生多次求值、类型歧义或隐藏控制流；普通函数和 `static inline` 能表达时必须优先使用。**

- 能用普通函数或 `static inline` 完成时，不使用函数式宏。
- 必须使用宏时，每个参数和整体结果都加括号。
- 每个宏参数在一条执行路径中只能求值一次；做不到时必须改用函数或 `static inline`。
- 调用函数式宏时不传入 `++`、`--`、赋值或函数调用等带副作用的实参。
- 多语句宏使用 `do { ... } while (0)`，不得隐藏 `return`、`goto`、资源获取或不可见控制流。
- 宏不能依赖调用点恰好存在的局部变量。
- 需要类型安全、调试断点或地址语义的操作必须使用函数。

```c
/**
 * @brief  将输入值限制在指定闭区间内
 * @param  value      待限制的输入值
 * @param  minimum    允许的最小值
 * @param  maximum    允许的最大值
 * @return 限制后的数值
 */
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

### 6.11 指针生命周期、对齐和别名

**STYLE-TYPE-007 [MUST] — 每个指针必须指向仍在有效期内、满足类型对齐且位于合法对象边界内的对象。**

- 不返回局部自动对象的地址，不把初始化函数栈上的配置、回调上下文或临时缓冲区保存到长期对象。
- 指针算术只在同一数组对象及其尾后一位置范围内进行，尾后指针只能比较，不能解引用。
- 将字节地址转换为更严格对齐的类型前必须验证地址；可能未对齐的数据按第 6.7 节处理。
- 对象指针与整数之间的转换只在硬件地址或 ABI 边界使用 `uintptr_t`/`intptr_t`，并记录平台保证；函数指针不与对象指针互换。
- 普通对象只通过其有效类型、兼容类型或字符类型访问；表示转换使用 `memcpy` 或显式字节解析，不使用指针强转或联合体读取实施类型双关。
- `restrict` 只用于调用方能够保证对应对象在整个访问期间不重叠的私有性能接口，并在函数契约中说明该前置条件。

## 7. 函数和控制流

本章规定函数在 C 代码中的声明、参数、返回值和局部控制流。

**STYLE-CTRL-001 [MUST] — 函数必须具有单一且可描述的职责、完整原型和明确的调用语义。**

- 一个函数只承担一个可解释的职责；存在多个独立处理阶段时提取辅助函数。
- 每个函数都显式声明返回类型；无返回值使用 `void`，无参数函数使用 `(void)`。
- 函数名、参数、返回值和必要注释必须让调用者能够判断其直接行为。
- 函数不通过未声明的全局状态暗中传递普通结果或错误。

### 7.1 参数与返回值

**STYLE-CTRL-002 [MUST] — 参数和返回值必须无歧义地表达输入、输出、有效性和失败，调用者必须处理所有影响正确性的结果。**

- 输入参数在前，输出参数在后；输入结构体指针使用 `const`。
- 参数超过 4～5 个且属于同一配置时，SHOULD 使用配置结构体。
- 不用返回值同时表达“数据”和“错误”而导致歧义；使用状态码加输出参数，或返回带有效标志的结果类型。
- 返回模块所属的枚举状态，禁止使用无名称的 `-1`、`-2` 表达错误。
- 简单 getter 可以直接返回值，但数据可能无效时必须提供状态、时间戳或有效标志。
- 状态码、写入长度、初始化结果和可能失败的查询结果必须被检查并进入明确控制流。
- 只有在接口契约允许忽略且后果已知时才能显式转换为 `(void)`；忽略原因不明显时必须添加说明。
- 输出参数只在成功时有效，或由接口契约明确规定各失败结果下的有效成员和初始状态。

```c
sensor_status_t sensor_get_sample(
    const sensor_handle_t *sensor,
    sensor_sample_t *sample);
```

### 7.2 参数校验深度

**STYLE-CTRL-003 [MUST] — 参数校验必须与接口公开程度、数据来源和已声明的前置条件一致。**

| 接口位置 | 校验要求 |
| --- | --- |
| 公共初始化接口 | 检查空指针、范围、配置组合和依赖状态 |
| 外部数据入口 | 检查指针、长度、帧边界、校验和和版本 |
| 普通公共控制接口 | 检查对象状态和可能来自运行时的参数 |
| 模块私有函数 | 依赖明确前置条件，只保留关键防御 |

参数来自编译期并不自动代表安全；只有调用边界和不变量真正受控时才能省略范围检查。

### 7.3 空指针与布尔判断

**STYLE-CTRL-004 [MUST] — 指针、布尔值、数值和硬件状态必须按各自语义判断，不得依赖隐式真假掩盖类型或协议错误。**

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

**STYLE-CTRL-005 [MUST] — 普通循环必须具有可证明的终止条件；有意的无限循环和轮询等待必须具有明确且可审查的退出语义。**

- 空循环体必须使用花括号和注释说明目的。
- 轮询硬件状态时，循环必须具有成功、失败或超时退出条件。
- 循环内部不额外修改循环控制变量。
- 有意的无限循环必须位于职责明确的入口函数中，并在代码或接口名称中说明其不返回语义。
- 循环上界不能从代码直接判断时，使用具名常量或注释说明其限制。

```c
start_time_ms = system_time_ms();
while (!uart_reset_done()) {
    elapsed_time_ms = system_time_ms() - start_time_ms;
    if (elapsed_time_ms >= UART_RESET_TIMEOUT_MS) {
        return UART_STATUS_TIMEOUT;
    }
}
```

无符号计数器的减法按模数定义。采用这种写法时，时间接口必须规定最大持续时间；不得用普通大小比较自行实现可能回绕的绝对 deadline。

### 7.5 统一清理路径

**STYLE-CTRL-006 [MUST] — 获取多个资源的函数必须在每条退出路径上按相反顺序释放已经获得的资源。**

当函数按顺序获取多个资源时，可以使用向后的 `goto` 统一释放；标签描述需要执行的清理阶段。

```c
/**
 * @brief  写入一组存储数据
 * @param  storage   已初始化的存储实例
 * @param  data      待写入数据
 * @param  length    待写入字节数
 * @retval STORAGE_STATUS_OK               写入成功
 * @retval STORAGE_STATUS_INVALID_ARGUMENT 参数无效
 * @retval STORAGE_STATUS_BUSY             存储设备当前不可用
 * @retval STORAGE_STATUS_IO_ERROR         使能或传输阶段发生硬件错误
 */
storage_status_t storage_write(storage_t *storage, const uint8_t *data, size_t length)
{
    storage_status_t status;

    if ((!storage) || ((!data) && (length > 0U))) {
        return STORAGE_STATUS_INVALID_ARGUMENT;
    }

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

### 7.6 递归和 VLA

**STYLE-CTRL-007 [MUST] — 递归和 VLA 默认禁止，例外必须具有可证明的栈使用上限。**

- 递归默认 MUST NOT；只有栈深度有严格静态上限、经过分析且平台允许时才能例外。
- VLA MUST NOT；使用编译期数组、调用者缓冲区或固定对象池。

### 7.7 副作用和可重入性

**STYLE-CTRL-008 [MUST] — 函数的直接副作用和并发调用限制必须在名称、接口或必要注释中可见。**

函数的隐藏副作用越少越好。读取接口不应同时清除状态，除非名称使用 `take`、`consume`、`read_and_clear` 等明确表达。回调注册、全局配置和硬件状态改变必须写入契约。

函数不得为了局部便利临时增加未在接口或文件级说明中出现的共享可写状态。

### 7.8 应避免的关键字

**STYLE-CTRL-009 [MUST] — 影响栈界限、控制流可追踪性或工具分析能力的语言特性必须受限。**

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

## 8. C 运行库与嵌入式限制

**STYLE-LIB-001 [MUST] — C 运行库函数只有在目标实现提供且其代码体积、栈、最坏执行时间、重入性和失败语义满足调用路径要求时才能使用。**

| 类别 | 默认策略 | 说明 |
| --- | --- | --- |
| `malloc`/`free` | 限制 | 只通过项目规定的内存接口使用，并检查失败 |
| `printf`/格式化 | 限制 | 不在 ISR；评估代码体积、执行时间和缓冲区 |
| `strcpy`/`strcat`/`gets` | 禁止 | 使用显式长度和容量的接口 |
| `sprintf`/`vsprintf` | 禁止 | 使用经验证的限长格式化或专用编码器 |
| `scanf` 系列 | 限制 | 外部输入优先使用长度受限的专用解析器 |
| `memcpy`/`memset` | 允许 | 长度必须经过边界验证 |
| `assert` | 开发期允许 | 发布版本定义失败策略 |
| `rand` | 避免 | 需要随机性时使用硬件或经过说明的实现 |

MCU 工具链可以提供 freestanding 实现、裁剪后的 C 库或厂商替代库。项目必须以实际链接实现和链接选项为依据；函数在 ISO C 中存在不代表该目标完整提供，也不代表适合 ISR 或实时路径。

### 8.1 字符串和格式化

**STYLE-LIB-002 [MUST] — 文本处理必须始终携带可验证的长度和容量，格式字符串必须与实际提升后的参数类型匹配。**

- 工具链实现经过验证时使用 `snprintf`，容量来自真实数组的 `sizeof` 或接口参数。
- 检查返回值是否为负或大于等于容量，以识别编码失败和截断。
- 外部输入不保证以 `\0` 结尾；解析时始终携带长度。
- 不用 `strncpy` 假设结果一定终止；需要时显式写入末尾零字符。
- `sizeof(pointer)` 不是指向缓冲区的容量；数组传入函数后必须同时传递容量。
- 可变参数格式化依照默认参数提升传入类型，并使用匹配的格式说明符。
- 二进制协议不使用字符串 API。

```c
int written = snprintf(
    buffer,
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

**STYLE-LIB-003 [MUST] — 字节复制、移动、比较和清零必须验证对象边界、重叠关系、表示语义和硬件所有权。**

`memcpy` 的源和目标不可重叠；可能重叠时使用 `memmove`。长度必须先证明不超过源对象和目标对象的有效范围，并且不能由加法溢出产生。

复制结构体前确认结构体不包含需要重新绑定的指针、填充敏感数据、同步对象或硬件所有权。`memcmp` 只比较明确的字节序列，不用于判断普通结构体的值相等。清零结构体只在全零确实代表所有成员的合法初始状态时使用；敏感数据擦除必须采用编译器不会优化掉的目标实现。

### 8.3 断言

**STYLE-LIB-004 [MUST] — 断言只验证程序员能够保证的内部不变量，不得替代正常错误处理。**

断言用于发现程序员错误和内部不变量，不用于处理外部输入、通信错误或正常可恢复故障。代码正确性不能只依赖可能被编译掉的断言。

### 8.4 编译器扩展

编译器扩展按 `STYLE-LANG-002` 管理。`__attribute__`、内联汇编、段属性、弱符号和编译器内建函数集中在平台边界或兼容宏中。公共上层接口不直接暴露扩展。使用扩展时说明工具链、目标、生成代码要求和无扩展时的处理方式。

### 8.5 数值转换和控制流库函数

**STYLE-LIB-005 [MUST] — 外部文本转换和固件终止必须返回可区分的结果，不得依赖无法报告完整错误的便利函数或主机进程语义。**

- `atoi`、`atol`、`atof` MUST NOT 用于外部或持久化输入，因为它们不能可靠区分合法值、格式错误和范围溢出。
- `strtol`、`strtoul`、`strtof` 等函数只有在目标 C 库的链接体积、结束指针、范围错误、`errno` 和 RTOS 重入行为均已验证时采用。
- 通信帧、命令和持久化字段优先使用带长度参数、范围检查和明确状态码的项目解析器。
- 手写裸机和 RTOS 固件禁止调用 `abort`、`exit`、`_Exit`、`setjmp` 和 `longjmp`；这些接口依赖的进程终止或非局部跳转语义不作为固件控制流使用。

```c
parse_status_t parse_u32(
    const char *text,
    size_t text_length,
    uint32_t minimum,
    uint32_t maximum,
    uint32_t *value);
```

## 9. 状态码与边界检查

本章只规定状态码在 C 接口中的表示、检查和输入边界校验。

### 9.1 状态码

**STYLE-ERR-001 [MUST] — 可能失败的 C 接口必须使用具有稳定语义的状态类型，并且调用者必须处理返回结果。**

每个模块拥有自己的状态类型，或使用项目统一且语义稳定的公共状态类型。成功值固定且可读，错误值不通过“神秘负数”传播。

```c
typedef enum {
    UART_STATUS_OK = 0,             /* 操作成功 */
    UART_STATUS_INVALID_ARGUMENT,   /* 输入参数无效 */
    UART_STATUS_NOT_INITIALIZED,    /* UART 实例尚未初始化 */
    UART_STATUS_BUSY,               /* UART 正在执行其他操作 */
    UART_STATUS_TIMEOUT,            /* 操作未在规定时间内完成 */
    UART_STATUS_IO_ERROR,           /* 底层外设或传输发生错误 */
} uart_status_t;
```

协议、持久化和 ABI 使用状态码时，每个成员具有显式数值并编码为规定宽度的整数；仅在 C 模块内部使用时不依赖枚举对象的底层宽度。

不要假设不同枚举类型可以互换；需要转换时逐项显式映射。每个状态返回值必须被检查、返回、映射或依据接口契约显式忽略，不允许无意丢弃。

### 9.2 边界防御

**STYLE-ERR-002 [MUST] — 外部字节、长度、索引和数值在用于访问对象或参与运算前必须完成适用的格式与范围检查。**

外部通信、DMA 长度、传感器样本和持久化字节必须先验证，再用于数组访问、指针运算、内存复制、枚举转换或算术。模块内部受控调用可以依赖已经声明的前置条件，避免无意义地重复同一检查。

## 10. 编译与静态检查

### 10.1 编译警告

**STYLE-TOOL-001 [MUST] — 所有手写固件必须使用明确的 C 方言、高警告级别和项目代码零警告策略编译。**

GCC/Clang 工具链可从以下集合选择目标版本实际支持且适合项目的选项：

```text
-std=c11 -Wall -Wextra -Wconversion -Wshadow -Wformat=2 -Wundef
-Wstrict-prototypes -Wmissing-prototypes
```

项目必须记录没有采用的关键检查及原因。`-Werror` 用于手写代码的 CI 或正式构建；芯片厂商、生成代码和第三方依赖使用独立目标、局部抑制或明确排除，不得迫使项目关闭自身警告。不同编译器不机械复制选项名称，应选择语义等价的诊断。

每个抑制都应尽量局部，并说明为什么是误报或为什么当前设计安全。不得通过无意义类型转换、初始化或 `(void)` 大量吞掉真实问题。

优化级别、链接时优化、浮点 ABI 和 `fast-math` 等影响语义或 ABI 的选项必须由目标构建统一管理。`fast-math` 默认禁止；确需采用时必须验证 NaN/Inf、舍入、控制稳定性和生成代码，并登记例外。

### 10.2 静态断言和编译期检查

**STYLE-TOOL-002 [MUST] — 能在编译期证明的配置关系、容量和外部布局约束必须使用静态断言或等价构建检查。**

使用 `_Static_assert` 验证协议大小、数组关系、配置上限和类型假设：

```c
_Static_assert(MOTOR_COUNT <= 8U, "motor bitmap only supports eight instances");
_Static_assert(UART_RX_CAPACITY > UART_MAX_FRAME_SIZE,
               "UART RX buffer must hold the largest complete frame");
```

不要断言标准类型名称本身已经保证的事实；验证项目真正依赖的 `CHAR_BIT`、对象布局、字段偏移、数组关系和硬件约束。

### 10.3 静态分析

**STYLE-TOOL-003 [MUST] — 静态分析结果必须逐项检查，并分类为修复、带技术依据的抑制或已登记问题。**

静态分析重点关注：越界、空指针、未初始化、整数溢出、死代码、资源泄漏、并发访问和未定义行为。工具报告的处理结果分为修复、带依据抑制或已登记问题，不允许不经检查整体关闭规则集。

## 11. 工具和检查

**STYLE-TOOL-004 [MUST] — 仓库根目录的 `.clang-format` 必须由 LLVM clang-format 21.x 执行，格式检查必须覆盖本次修改的全部手写 `.c` 和 `.h` 文件。**

`.clang-format` 使用无 `Language` 限定的默认配置段，使 `.c` 与 `.h` 采用同一组排版选项；格式化范围仍只包含本规范适用的 C 文件。项目固定 LLVM 21.x，不能以未记录的其他主版本运行后提交格式结果。CI 可使用以下等价命令：

配置项名称和取值以 [LLVM clang-format 21.1 文档](https://releases.llvm.org/21.1.0/tools/clang/docs/ClangFormatStyleOptions.html) 为准。

```sh
rg --files -0 -g '*.c' -g '*.h' | xargs -0 clang-format-21 --style=file --dry-run --Werror
```

代码规范检查包括格式与文本卫生、编译器诊断和静态分析。项目可以根据工具链调整具体命令，但不能降低“无新增警告、无未解释静态分析问题、无尾随空白”的基本要求；测试与发布检查参照仓库工作流指南执行。

个人 `.clang-format` 是自动格式化基线；无法由工具表达的规则按本文档正文进行代码审查。

## 12. 检查清单

- [ ] 公共符号有模块前缀，私有符号已使用 `static`。
- [ ] 符合性记录包含规范版本、C 标准、编译器/ABI、MCU、clang-format 版本和例外。
- [ ] 列宽不超过 100，文件无 Tab 和尾随空白。
- [ ] include 按规定顺序连续排列，类别之间没有空行。
- [ ] 头文件自洽且没有泄露实现细节。
- [ ] `.h` 文件没有 Doxygen 函数注释和文件头注释。
- [ ] 每个手写 `.c` 函数定义都有 `@brief`、必要的 `@param` 以及正确的 `@return`/`@retval`。
- [ ] 每个宏定义和每个枚举成员都有各自对应的中文注释。
- [ ] 函数显式声明返回类型，状态返回值已经处理或依据契约显式忽略。
- [ ] C 标准、`CHAR_BIT`、整数宽度、ABI 和外部布局假设已经验证。
- [ ] 指针生命周期、长度、索引、位移、整数提升和窄化转换经过检查。
- [ ] 协议和持久化数据没有依赖枚举、位域或普通结构体的实现相关布局。
- [ ] `volatile` 没有被用来代替原子操作、临界区、内存屏障或 cache 维护。
- [ ] 轮询中的时间计算符合项目时间接口的回绕契约。
- [ ] 运行库调用满足目标实现的体积、栈、最坏时间、重入性和失败处理要求。
- [ ] clang-format 21.x、编译警告和静态分析已经执行。
- [ ] 新增例外已记录原因、影响和验证方式。

## Changelog

### v1.1.1 — 2026-07-24

- 更新仓库工作流指南的文件名与引用说明，不改变代码风格规则。

### v1.1.0 — 2026-07-24

- 将适用范围收敛为 MCU 裸机/RTOS C 固件，建立稳定规则 ID、符合性记录和例外机制。
- 明确 C11 最低基线、工具链扩展边界，以及 BARR-C、Linux 内核通用 C 风格和 ISO C/WG14 的参考关系。
- 完善整数表达式、指针生命周期、布局与序列化、ISR/DMA/RTOS 共享数据、运行库和超时回绕规则。
- 统一 `.c` Doxygen 返回语义和文件头字段，保持 `.h` 普通分组注释及宏、枚举成员逐项注释。
- 固定 clang-format 21.x，补充编译警告、静态分析和符合性检查清单。

### v1.0.0 — 2026-07-20

- 发布首个正式版本，建立通用嵌入式 C 命名、排版、注释、类型和防御式编码基线。
- 规定 `.c` 函数与文件头的 Doxygen 格式，头文件注释边界，以及宏和枚举成员的逐项注释。
- 采用 100 列、4 空格缩进、Linux 花括号和顺序连续的 include 排列。
- 建立 C11 语言基线、固定宽度类型、所有权、标准库、错误码、编译警告和静态检查规则。
- 记录 BARR-C:2018 和 Linux 内核编码规范的参考边界及个人嵌入式工程取舍。
