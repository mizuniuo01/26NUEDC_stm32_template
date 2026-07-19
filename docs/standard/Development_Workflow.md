# 嵌入式项目通用开发流程规范

> 版本：1.0  
> 适用范围：MCU 裸机、RTOS、厂商代码生成工程，以及包含少量 Python 工具、视觉模型或上位机辅助程序的混合仓库  
> 当前实践参考：STM32、TI MSPM0、ESP32 Arduino；本文不绑定任何芯片、IDE 或构建系统  
> GitHub 功能复核日期：2026-07-17

本文规定一个嵌入式项目从建仓、开发、测试、评审到发布的通用工作方式。它是规范，不是某个工具的操作教程；命令和模板只用于明确规则、风险边界和可验证结果。

---

## 0. 如何使用本规范

### 0.1 规则等级

| 等级 | 含义 | 执行要求 |
| --- | --- | --- |
| **必须（MUST）** | 缺失后会破坏可构建性、可追溯性、安全性或协作基础 | 除非项目明确记录例外，否则必须执行 |
| **应该（SHOULD）** | 成熟项目的默认选择 | 可以裁剪，但应说明原因 |
| **可以（MAY）** | 在规模、团队或发布方式需要时启用 | 不应为了“看起来专业”而机械加入 |

### 0.2 项目成熟度

| 级别 | 典型场景 | 最低要求 |
| --- | --- | --- |
| **个人基线** | 学习、比赛、原型、个人长期项目 | 可复现构建、README、Git、原子提交、基本测试、无敏感信息 |
| **协作增强** | 两人以上、长期维护、跨设备开发 | PR、CI、Issue 模板、分支保护、评审、明确依赖和工具链版本 |
| **开源/交付增强** | 对外发布、客户交付、可复用库 | License、贡献与安全政策、稳定版本、Release 制品、变更记录、供应链安全 |

除非章节另有说明，“必须”适用于所有级别。团队和开源要求是在个人基线上叠加，而不是另一套互不兼容的流程。

### 0.3 总原则

1. **仓库必须能够解释自身**：新环境只依赖仓库文档即可完成配置、构建和基本验证。
2. **逻辑职责比目录名字重要**：厂商工具已经规定目录时，不得为追求表面统一而破坏生成流程。
3. **源码、配置源和发布结果必须可追溯**：任何固件都应能定位到 Commit、工具链、构建配置和硬件目标。
4. **`main` 必须保持健康**：进入 `main` 的内容必须通过项目规定的检查。
5. **自动化必须服务于重复性和风险控制**：不能稳定执行或无人维护的自动化不算工程能力。
6. **规范允许裁剪，但不允许沉默偏离**：例外应记录在 README、ADR 或贡献指南中。

### 0.4 本文边界

- C 代码命名、格式和语言规则由 [CODING_STYLE.md](CODING_STYLE.md) 负责；嵌入式分层、模块边界和设计模式由
  [ARCHITECTURE_STANDARD.md](ARCHITECTURE_STANDARD.md) 负责。Python 遵循 PEP 8 和项目选定的格式化/静态检查配置。
- 本文不规定电气设计、功能安全认证、MISRA 合规或产品级质量体系。
- 嵌入式 Linux 只在 §6 说明未来目录变化，不覆盖内核、Yocto 或 Buildroot 的完整流程。

---

# 第一部分：仓库与目录结构

## 1. 目录设计方法

### 1.1 不存在唯一的“行业标准目录”

嵌入式工程会受厂商生成器、IDE、SDK、构建系统、芯片数量和固件数量约束，因此不存在一棵对所有项目都正确的固定目录树。专业做法是统一以下内容：

- 每类文件的职责和所有权清晰。
- 生成代码与手写代码可区分。
- 构建输入与构建输出可区分。
- 上层业务不依赖不必要的芯片细节。
- 第三方代码的来源、版本和修改可追溯。
- 实际目录与通用职责不一致时，在 README 中给出映射。

本文后续目录名是推荐名，不是必须强行采用的物理路径。

### 1.2 推荐的逻辑分区

```text
project/
├── app/                    # 应用逻辑、状态机、任务编排
├── include/                # 项目公开头文件；也可与模块源码内聚
├── src/                    # 通用源码；也可改用按模块组织
├── services/               # 日志、存储、通信等跨业务服务
├── bsp/                    # 板级资源映射和板级初始化
├── drivers/                # 项目自有设备/外设驱动
├── platform/               # MCU、SDK、框架、编译器、OS 适配
├── rtos/                   # RTOS 端口与集中配置；无 RTOS 时不创建
├── boards/                 # 多板卡定义；单板项目可并入 bsp/
├── config/                 # 可版本化的产品/构建配置
├── startup/                # 启动文件；若厂商目录固定则保留原位
├── linker/                 # 链接脚本和内存布局
├── tests/
│   ├── unit/               # 可在主机运行的单元测试
│   ├── integration/        # 模块组合与协议测试
│   └── hil/                # Hardware-in-the-loop
├── tools/                  # 项目附带的工具程序
├── scripts/                # 构建、检查、烧录、打包等自动化入口
├── docs/                   # 详细工程文档
├── examples/               # 可独立运行的用法示例
├── hardware/               # 原理图、BOM、引脚表及硬件版本说明
├── third_party/            # 通用第三方源码
├── vendor/                 # 芯片厂商 SDK、HAL、CMSIS 等
├── assets/                 # 字库、图片、校准数据等资源
├── models/                 # 机器学习模型及其元数据
├── .github/                # GitHub 模板、Actions 和依赖自动化
├── build/                  # 本机构建输出，不进入 Git
├── dist/                   # 本地打包暂存，不作为版本来源
└── <root files>            # README、构建入口、许可证等
```

小型工程不应创建大量空目录。只有出现相应职责时才创建目录，并在 README 的目录树中解释。

### 1.3 两种源码组织方式

#### 分层组织

适用于小型固件和层次稳定的项目：

```text
app/ → services/ → drivers/ → platform/vendor
```

- `app/` 负责业务决策，不直接操作寄存器。
- `services/` 提供存储、日志、通信、时间等可复用能力。
- `drivers/` 驱动具体器件或外设。
- `platform/` 隔离芯片、SDK、RTOS 和编译器差异。

这是一条依赖方向，不要求每次调用都机械经过所有层。禁止为了“分层”创建只转发参数、没有抽象价值的空壳模块。

#### 按模块内聚

适用于模块较多或功能边界清晰的项目：

```text
src/
├── motor/
│   ├── motor.c
│   ├── motor.h
│   └── motor_internal.h
├── perception/
└── protocol/
```

- 一个模块的源码、私有头文件和测试尽量靠近。
- 对外头文件可以放在模块目录，也可以集中放入 `include/`。
- 项目必须统一选择，不能一部分按层、一部分按模块且没有说明。

## 2. 重点目录规范

### 2.1 `app/`：应用层

存放产品行为、状态机、模式管理、任务编排和系统级策略。

**必须：**

- 通过明确接口访问驱动和平台能力。
- 将“做什么”与“如何操作某芯片外设”分开。
- 裸机循环、RTOS 任务或 Arduino `loop()` 只是调度入口，不应承载全部业务实现。

**不得：**

- 在多个应用模块中复制相同寄存器或引脚操作。
- 让中断服务函数直接执行复杂业务、阻塞等待或动态分配大对象。

### 2.2 `bsp/` 与 `boards/`：板级支持

`bsp/` 描述一块板上“LED1、MOTOR_LEFT、SENSOR_I2C”如何映射到实际外设和引脚；`boards/` 用于存在多个板卡或硬件版本时选择具体 BSP。

应包含：

- 板级初始化入口。
- 引脚和外设实例映射。
- 时钟、供电控制和板级资源冲突说明。
- 硬件修订版差异。

不应把可跨板复用的器件协议算法放入 BSP。

### 2.3 `drivers/`：项目自有驱动

用于传感器、执行器、显示器、通信模组和项目自有的外设封装。

- 驱动接口应表达器件能力，而不是泄漏所有底层句柄。
- 驱动应显式处理超时、错误状态和生命周期。
- 可测试的协议解析、校验和状态机应与真实 I/O 分离。
- 厂商原始 HAL/SDK 不应与项目自有驱动混放。

### 2.4 `platform/` 与 `port/`：平台适配

隔离芯片系列、SDK、编译器、RTOS、Arduino 框架或宿主机测试环境差异。

典型内容：

- 时间、临界区、原子操作、日志后端。
- UART/SPI/I2C/GPIO 的最小适配接口。
- 编译器属性、段定义和字节序处理。
- RTOS 与裸机的同步原语适配。
- 主机单元测试所需的 fake/mock 后端。

平台层不是第二套无边界 HAL。只抽象项目确实需要替换或测试的能力。

### 2.5 `rtos/`：RTOS 相关内容

只有使用 RTOS 时才创建，存放集中配置、端口适配、系统钩子和静态资源声明。

- 业务任务仍属于对应业务模块，不应把所有 `*_task.c` 集中堆进 `rtos/`。
- 任务优先级、栈大小、周期和看门狗要求必须集中记录。
- 中断与任务的通信方式必须明确，禁止无界队列和不可证明的阻塞。
- RTOS 内核本身属于 `vendor/`、`third_party/` 或依赖管理器，不属于项目自有源码。

### 2.6 `config/`：配置

只存放可安全进入版本控制的配置源：

- 板卡、产品型号和构建变体。
- 功能开关及其默认值。
- 协议、标定或测试的非敏感默认配置。

密钥、令牌、私人 Wi-Fi 密码、证书私钥和个人绝对路径不得提交。需要说明变量名时提交 `.env.example` 或脱敏模板。

### 2.7 `tests/`：测试

| 子目录 | 目的 | 是否依赖硬件 |
| --- | --- | --- |
| `unit/` | 算法、协议、状态机、边界条件 | 否 |
| `integration/` | 多模块、文件格式、通信帧、构建组合 | 通常否；需要时明确 |
| `hil/` | 烧录、真实 I/O、传感器或负载测试 | 是 |

每个测试必须给出可判定的通过条件。串口打印“看起来正常”只能作为调试证据，不能代替自动判定。

### 2.8 `tools/` 与 `scripts/`

- `tools/` 存放有独立功能、需要维护或可能有自身依赖的工具程序。
- `scripts/` 存放项目工作流入口，例如 `build`、`check`、`flash`、`package`、`gen_version`。
- 脚本应从仓库根目录或自身位置可靠定位文件，不依赖开发者私人绝对路径。
- 关键脚本必须支持非交互执行，以便 CI 调用。
- 同一动作应该只有一个权威入口；README、开发者和 CI 调用同一个脚本或构建目标。

### 2.9 `docs/` 与 `hardware/`

`docs/` 应按信息职责组织，而不是成为文件垃圾箱：

```text
docs/
├── architecture.md        # 系统边界、模块关系、关键数据流
├── build.md               # 详细环境与构建说明
├── debug.md               # 日志、调试器、故障定位
├── protocol.md            # 通信帧、时序、兼容规则
├── testing.md             # 测试环境、用例和验收标准
├── release.md             # 发布和回滚要求
├── adr/                   # Architecture Decision Records
└── migration/             # 硬件、SDK、协议或版本迁移
```

`hardware/` 可以保存可再分发的原理图、BOM、接口定义、引脚表和硬件修订说明。厂商手册和数据表可能受再分发许可约束；无法确认时应保存官方链接、文档编号和校验信息，而不是直接复制进公开仓库。

### 2.10 `third_party/`、`vendor/` 与外部依赖

| 位置 | 内容 | 要求 |
| --- | --- | --- |
| `third_party/` | 与芯片厂商无关的第三方源码 | 记录来源、版本、许可证和本地修改 |
| `vendor/` | 芯片/框架厂商提供的 SDK、HAL、CMSIS、生成支持文件 | 保持来源边界，不与自有代码混改 |
| `external/` | Git submodule 或外部源码工作区 | 只有项目确实采用该机制时使用 |

依赖选择顺序没有绝对标准，应按下列条件判断：

1. 官方包管理器或可锁定版本的依赖机制通常优先。
2. 直接 vendoring 适合离线、长期可复现或上游不稳定的依赖，但必须保留许可证和版本来源。
3. Git submodule 适合需要独立历史的仓库依赖；如果团队不熟悉其初始化、更新和 CI 行为，不应仅为“高级”而采用。
4. 修改第三方代码时，应保存补丁、独立 fork 或清晰的 `README`，不得让修改与上游版本无法区分。

### 2.11 `assets/`、`models/` 与大文件

- 每个模型或二进制资源应该记录格式、来源、许可证、输入输出、量化方式、目标硬件和生成脚本。
- 能够从源数据和脚本可靠生成的文件原则上不进入 Git；生成成本过高时可以纳入制品存储。
- 大文件是否使用 Git LFS 应根据平台配额、克隆成本和离线需求决定。
- 不得只提交一个含义不明的 `model.bin`；至少应有版本或元数据文件与其关联。

### 2.12 `build/` 与 `dist/`

- `build/` 是可删除、可重新生成的本地中间目录，必须加入 `.gitignore`。
- `dist/` 是本地发布打包暂存区，通常也不进入 Git。
- 正式固件通过 GitHub Release 或批准的制品库保存，不通过提交二进制到源码历史发布。
- 如果法规或离线交付要求将制品纳入专用仓库，应与源码仓库分离并保存校验和及来源 Commit。

## 3. 根目录文件规范

### 3.1 文件分级总表

| 文件 | 个人基线 | 协作 | 开源/交付 | 主要职责 |
| --- | :---: | :---: | :---: | --- |
| `README.md` | 必须 | 必须 | 必须 | 项目入口与快速验证 |
| `.gitignore` | 必须 | 必须 | 必须 | 排除可再生和私有文件 |
| `.gitattributes` | 应该 | 必须 | 必须 | 行尾、文本/二进制、LFS 属性 |
| 构建入口 | 必须 | 必须 | 必须 | 唯一、可自动执行的构建方式 |
| `LICENSE` | 按需 | 按需 | 必须 | 使用、复制和分发权利 |
| `CHANGELOG.md` | 发布时应该 | 必须 | 必须 | 面向使用者的版本变化 |
| `CONTRIBUTING.md` | 可以 | 应该 | 必须 | 贡献和评审流程 |
| `SECURITY.md` | 可以 | 应该 | 必须 | 漏洞报告与支持版本 |
| `.editorconfig` | 应该 | 必须 | 应该 | 基础编辑器一致性 |
| 格式化/静态检查配置 | 应该 | 必须 | 必须 | 机器可执行的代码规范 |
| `.env.example` | 按需 | 按需 | 按需 | 环境变量名和非敏感示例 |
| `CODE_OF_CONDUCT.md` | 不需要 | 可以 | 社区型项目应该 | 社区行为准则 |
| `SUPPORT.md` | 可以 | 可以 | 用户型项目应该 | 支持渠道与问题边界 |

“按需”表示只有相应技术或发布需求存在时才创建，不是低优先级占位文件。

### 3.2 `README.md`

README 是仓库入口，不是所有设计文档的合集。必须回答：

1. 项目解决什么问题，目前处于什么状态。
2. 支持哪些硬件、板卡或关键框架。
3. 需要哪些工具及版本。
4. 如何取得依赖、配置、构建和找到产物。
5. 如何烧录或运行最小验证。
6. 目录和架构从哪里继续阅读。
7. 已知限制、版本和许可证是什么。

命令必须可以复制执行；路径、产物名和参数必须与仓库一致。详细故障排查应链接到 `docs/debug.md`，不要无限扩充 README。

### 3.3 `LICENSE`

- 公开源码不等于允许他人使用；公开或分发项目必须明确许可证。
- 不得默认所有个人项目都使用 MIT，应根据第三方依赖、交付条件和使用目的选择。
- 第三方许可证文件不得删除。
- 私有且不对外分发的仓库可以不放开源许可证，但对外提供二进制时仍需检查依赖义务。

### 3.4 `CHANGELOG.md`

- 面向固件使用者记录可见变化，不复制全部 `git log`。
- 顶部保留 `Unreleased`。
- 版本倒序，使用 Added、Changed、Deprecated、Removed、Fixed、Security 分类。
- 每个正式 Release 前更新；自动生成内容必须经过人工整理。
- 内部重构只有影响使用、性能、兼容或风险时才进入 Changelog。

### 3.5 `.gitignore`

应该忽略：

- 构建目录、中间文件、缓存、日志和覆盖率输出。
- 用户级 IDE 状态、临时文件和操作系统垃圾。
- 本地密钥、真实 `.env`、私人证书和烧录凭据。

不应机械忽略：

- 构建系统真正需要的工程配置。
- 可共享的调试/任务配置。
- 厂商生成器的配置源，例如项目配置描述文件。

已经进入 Git 的文件不会因为后来加入 `.gitignore` 自动消失；应先判断它是否本来就应该被追踪。

### 3.6 `.gitattributes`

用于在不同操作系统上保持一致：

```gitattributes
* text=auto
*.c       text eol=lf
*.h       text eol=lf
*.cpp     text eol=lf
*.py      text eol=lf
*.sh      text eol=lf
*.bat     text eol=crlf
*.png     binary
*.pdf     binary
*.bin     binary
*.elf     binary
```

只有确定采用 Git LFS 时才添加相应 `filter=lfs` 规则。改变已有仓库的行尾策略必须作为独立变更完成并通知协作者，避免与业务修改混在同一 PR。

### 3.7 `.editorconfig` 与格式化配置

- `.editorconfig` 统一字符集、行尾、缩进和文件末尾换行。
- `.clang-format` 和 C 静态检查配置必须与 [CODING_STYLE.md](CODING_STYLE.md) 一致；架构检查边界必须与
  [ARCHITECTURE_STANDARD.md](ARCHITECTURE_STANDARD.md) 一致。Python 工具配置遵循 PEP 8 和项目约定。
- CI 应运行“检查模式”，不得在 CI 中悄悄改写代码后仍报告成功。
- 厂商代码可以从严格格式检查中排除，但排除路径必须明确。

### 3.8 构建入口与工具链文件

- 仓库必须存在一个权威构建入口，例如 CMake、Make、厂商命令行工程或统一脚本。
- IDE 的“点击 Build”不能是唯一构建方法；协作项目必须能非交互构建。
- 编译器、SDK、框架和关键工具版本必须被锁定或记录。
- Debug、Release、板卡和功能变体必须使用显式名称，不依赖开发者上次 GUI 状态。
- 工具链文件不得包含个人绝对路径。

### 3.9 `CONTRIBUTING.md`

协作或开源项目应说明：

- 环境和本地检查入口。
- 分支、Commit 和 PR 规则。
- 测试和硬件验证要求。
- 生成代码、第三方代码及格式化边界。
- Review、合并和发布权限。

本文件规定全局规范；项目的 `CONTRIBUTING.md` 只记录该仓库的具体落地方式。

### 3.10 `SECURITY.md`、`SUPPORT.md` 与社区文件

- `SECURITY.md` 说明受支持版本、私密报告渠道和禁止公开披露的内容。
- `SUPPORT.md` 区分 Bug、使用问题、硬件问题和商业支持。
- `CODE_OF_CONDUCT.md` 只在存在真实社区互动时启用。
- 这些文件不能只放空模板；联系人和流程必须真实有效。

### 3.11 `.env.example` 与秘密信息

`.env.example` 只提交变量名和安全示例：

```dotenv
WIFI_SSID=example-network
WIFI_PASSWORD=replace-me
DEVICE_API_URL=https://example.invalid
```

真实凭据必须放在本机安全存储、GitHub Secrets 或批准的密钥系统。删除当前文件不代表秘密已从 Git 历史和远端副本消失；发生泄漏时必须立即轮换秘密，再按安全流程清理历史。

### 3.12 其他常见根文件

| 文件 | 何时使用 | 规范要求 |
| --- | --- | --- |
| `CMakeLists.txt` | 采用 CMake | 顶层只负责项目、目标和子目录编排；工具链差异放入独立文件 |
| `CMakePresets.json` | 需要统一配置、构建和测试变体 | Preset 名稳定、无私人路径，README 直接引用 |
| `Makefile` | Make 是正式构建系统，或作为统一命令入口 | 不得与另一构建系统产生两套不一致逻辑 |
| `pyproject.toml` | 仓库包含受维护的 Python 工具/包 | 集中 Python 元数据、依赖和工具配置；依赖版本应可锁定 |
| `requirements*.txt` | Python 环境采用 requirements 工作流 | 区分直接依赖与锁定结果，不与 `pyproject.toml` 重复声明事实来源 |
| `Doxyfile` | 发布 C/C++ API 文档 | 输入和排除路径明确，CI 检查警告或生成 Pages |
| `VERSION` | 构建系统需要文件化版本源 | Tag、固件内版本和 Release 必须校验一致；不能多处手工维护 |
| `NOTICE` | 许可证或交付要求保留归属声明 | 内容必须能追溯到实际依赖和许可证 |
| `CITATION.cff` | 研究、论文、数据集或模型项目 | 作者、版本、DOI/URL 与 Release 同步 |
| `.clang-tidy` / 静态分析配置 | 项目启用对应工具 | 明确自有代码与厂商代码的检查边界 |
| `.pre-commit-config.yaml` | 团队希望提交前自动检查 | 只能作为快速反馈，CI 仍是权威检查 |
| `Dockerfile` / `.devcontainer/` | 需要可复现或隔离的开发环境 | 固定基础镜像/工具版本，不把密钥烘焙进镜像 |
| `AGENTS.md` 等工具说明 | 项目实际使用编码代理 | 只记录项目事实、命令和边界，不替代 README/CONTRIBUTING |

`compile_commands.json`、IDE 索引和生成的 API 页面通常是构建输出，不因某个工具能读取就自动进入 Git。是否追踪必须由可复现性和协作需求决定。

### 3.13 `.github/` 目录

```text
.github/
├── ISSUE_TEMPLATE/
│   ├── bug.yml                    # Bug Issue Form
│   ├── feature.yml                # Feature Issue Form
│   └── config.yml                 # 空白 Issue 和外部支持入口
├── workflows/
│   ├── ci.yml                     # PR/main 质量门
│   ├── release.yml                # Tag 发布
│   └── security.yml               # 可选安全扫描
├── PULL_REQUEST_TEMPLATE.md       # PR 必填信息和自检
├── CODEOWNERS                     # 可选路径所有者
├── dependabot.yml                 # 支持生态的依赖更新
└── release.yml                    # 自动 Release Notes 分类
```

- 文件名可以按项目需要调整，但一个职责应只有一个权威配置。
- Workflow 不得复制大段构建命令；应调用与本地相同的脚本或构建目标。
- 模板中引用的 Labels、路径、检查名和支持链接必须真实存在。
- 个人仓库不应创建空的 CODEOWNERS、安全扫描或发布工作流占位文件。

## 4. 厂商和框架特殊情况

### 4.1 通用处理原则

1. 找出配置源、生成结果、手写区域和构建输出。
2. 配置源必须进入 Git。
3. 是否提交生成结果取决于无 GUI/无专有工具时能否构建，以及官方工作流要求。
4. 无论是否提交，都必须记录生成工具和 SDK 版本。
5. 手工修改生成结果时必须能在下次生成后保留，或者以补丁/自动脚本重放。

### 4.2 简短映射示例

| 场景 | 配置源/入口 | 特殊注意 |
| --- | --- | --- |
| STM32CubeMX | `.ioc`、工程构建配置 | 保留 USER CODE 区；说明生成代码与自有模块映射 |
| TI MSPM0 SysConfig | `.syscfg`、SDK/编译器版本 | `ti_msp_dl_config.*` 等生成文件不得与自有驱动混为一层 |
| ESP32 Arduino | `.ino`/C++ 入口、库依赖和板卡配置 | `setup()`/`loop()` 保持轻量；业务实现仍按模块拆分 |
| RTOS 项目 | RTOS 配置、端口和任务资源表 | 任务不是架构层；同步、周期和栈预算必须可审查 |

这些示例用于识别边界，不规定具体 IDE 操作，也不要求不同厂商项目拥有完全相同的物理目录。

## 5. 多固件与混合仓库

### 5.1 何时采用一个仓库

以下条件同时满足时可以使用同一仓库：

- 多个固件属于同一产品并需要原子修改。
- 共用协议、板卡定义或测试工具。
- 可以用一个顶层入口分别构建和测试。

建议结构：

```text
firmware/
├── bootloader/
├── main_controller/
└── sensor_node/
shared/
tools/
```

不同产品、权限、发布周期或依赖完全独立时，应拆分仓库。

### 5.2 Python、视觉模型和上位机工具

- 每个子系统拥有自己的依赖清单和运行说明。
- 跨 MCU 与工具的通信协议必须有唯一文档和自动测试向量。
- 模型、固件和协议版本应能相互对应。
- 顶层 README 说明整体构建顺序，各子目录 README 只说明自身细节。

## 6. 嵌入式 Linux 迁移预留

开始嵌入式 Linux 后，仓库通常会新增 bootloader、kernel、device tree、root filesystem、应用服务和系统构建层。届时：

- 不应把 Linux 用户态应用继续当作 MCU `app/` 的简单扩展。
- 内核、设备树、rootfs 配方和产品应用应有明确边界。
- Yocto/Buildroot 层、软件包许可证、镜像签名和 OTA 会成为主要发布对象。
- 本文的 Git、Issue、PR、CI、Release 和安全规则仍然适用，但目录规范需要单独扩展。

---

# 第二部分：Git 提交与分支规范

## 7. Git 仓库基线

### 7.1 `main` 的含义

- `main` 是唯一默认长期分支。
- `main` 必须可构建，并通过项目声明的自动检查。
- `main` 上的每个 Commit 必须具有明确目的且可以追溯到 PR 或任务。
- 不维护常驻 `develop`；只有明确采用多版本并行模型时才增加长期维护分支。

“可发布”不代表每次合并都必须发布，而是主干不能依赖未提交文件、个人环境或已知损坏状态。

### 7.2 提交前检查

每次 Commit 前必须确认：

- 暂存区只包含本次目的相关修改。
- 没有构建产物、临时日志、密钥、私人路径或意外大文件。
- 能执行的最小构建和测试已经通过。
- 生成文件和配置源保持一致。
- 格式化修改没有掩盖无关业务变化。

### 7.3 Commit 签名

- Commit 签名证明提交与某个受验证身份/密钥关联，不证明代码正确或安全。
- 个人基线可以不强制签名；对外发布、高可信交付或组织政策要求时应该启用。
- 在 Ruleset 强制签名前，必须确认所有开发者、Web 合并方式和自动化账号都能生成可验证签名。
- 密钥丢失或泄漏必须撤销/轮换；不得为了保持绿色状态共用私人签名密钥。
- Release Tag 或发布制品签名比对所有临时分支 Commit 强制签名更直接，应根据威胁模型选择。

## 8. Conventional Commits

### 8.1 完整格式

```text
<type>(<scope>)!: <subject>

<optional body>

<optional footer(s)>
```

示例：

```text
feat(protocol): add frame sequence validation

Reject duplicate frames before updating the control state. This keeps
retransmission handling outside the application state machine.

Refs: #42
```

破坏性变更：

```text
feat(storage)!: change calibration record layout

BREAKING CHANGE: Existing calibration records must be regenerated.
```

### 8.2 Type

| Type | 使用场景 | 不应使用的场景 |
| --- | --- | --- |
| `feat` | 新增使用者或系统可用能力 | 仅调整参数或整理代码 |
| `fix` | 修复错误行为 | 没有行为变化的清理 |
| `docs` | 只修改文档 | 同时修改了代码行为 |
| `refactor` | 不改变外部行为的结构改进 | 性能或功能确实变化 |
| `perf` | 明确的性能、时延、内存或功耗改进 | 未测量的“感觉更快” |
| `test` | 新增或修正测试 | 修复产品代码本身 |
| `build` | 构建系统、工具链、依赖 | GitHub Actions 流程本身 |
| `ci` | CI/CD 和仓库自动化 | 普通构建脚本 |
| `chore` | 维护性杂项且不属于其他类型 | 用来掩盖无法分类的大提交 |
| `revert` | 撤销既有 Commit/PR | 普通反向修改 |

项目不得随意创造近义 Type，例如 `per`、`feature`、`bugfix`。确需扩展时必须写入项目贡献指南并配置自动检查。

### 8.3 Scope

- Scope 是受影响的稳定模块或子系统名，例如 `motor`、`protocol`、`board`、`build`、`docs`。
- Scope 使用小写英文名词，仓库内保持一致。
- 跨多个模块且无法选出主作用域时可以省略，不应写 `global`、`misc` 或罗列长名单。
- Scope 不是文件名，也不应使用临时任务编号代替。

### 8.4 Subject

- 必须使用英文，简洁说明“这个 Commit 完成什么”。
- 使用祈使或动作表达，例如 `add`、`fix`、`prevent`、`remove`。
- 首字母通常小写，末尾不加句号。
- 建议不超过 72 个字符；无法缩短时把原因和细节移入 body。
- 禁止 `update code`、`fix bug`、`work`、`final version` 等无信息描述。

### 8.5 Body 与 Footer

Body 应解释以下一种或多种信息：

- 为什么需要修改。
- 选择了什么设计及其约束。
- 行为、时序、内存、协议或兼容性如何变化。
- 测试和硬件验证中需要注意什么。

Footer 用于机器可读关联：

```text
Refs: #42
Closes: #57
Reviewed-by: Name
BREAKING CHANGE: <description>
```

Issue 只有在该 Commit/PR 完整解决时才使用 `Closes`；部分相关使用 `Refs`。

### 8.6 原子提交

一个 Commit 应代表一个可说明、可审查、可回退的逻辑变化：

- 功能实现及其直接测试可以放在同一 Commit。
- 纯格式化应与行为修改分开。
- SDK 升级、生成代码变化和业务适配应按可审查边界组织。
- 参数试验的中间过程不应全部进入主干；保留最终选择和必要依据。
- Commit 不要求每次都能烧录成完整产品，但不得故意留下编译错误。

## 9. 分支模型

### 9.1 分支命名

```text
<type>/<issue-id>-<short-description>
```

示例：

```text
feat/42-add-frame-sequence
fix/57-handle-uart-timeout
test/63-add-protocol-fuzz-cases
docs/71-document-board-revision
spike/try-new-scheduler
```

没有 Issue 的个人小任务可以省略编号：`docs/update-build-guide`。

规则：

- 全部小写，单词用连字符。
- 名称表达目标，不使用开发者姓名、日期或 `new`、`temp`。
- 分支生命周期应以天或少量周计算；长期未合并说明任务需要拆分或重新评估。

### 9.2 测试与实验分支

| 工作 | 应使用的分支 |
| --- | --- |
| 为一个新功能同时编写测试 | 同一个 `feat/` 分支 |
| 为一个 Bug 添加复现测试并修复 | 同一个 `fix/` 分支 |
| 建立全新的测试框架或 HIL 平台 | `test/` 分支 |
| 验证尚未决定采用的算法、SDK 或硬件方案 | `spike/` 分支 |
| 单纯调参数且结果尚不稳定 | `spike/` 分支或本地工作，不进入 `main` |

`spike/` 的结果只能以以下方式结束：

1. 放弃并删除分支，同时在 Issue/ADR 记录结论；或
2. 整理为正式 `feat/`、`refactor/` 等 PR，移除临时代码和调试残留。

### 9.3 `release/` 与 `hotfix/`

- 正常项目直接从 `main` 的 Tag 发布，不需要 `release/`。
- 只有在发布候选冻结后，`main` 仍需继续接收下一版本工作时才创建短期 `release/x.y`。
- `hotfix/` 只用于修复已经发布且需要独立维护的版本。
- 修复完成后必须同时回到仍受支持的主线，不能形成永久分叉。

## 10. GitHub Flow

### 10.1 标准闭环

```text
Issue/明确目标
      ↓
更新本地 main
      ↓
创建短生命周期分支
      ↓
开发 + 原子 Commit + 本地检查
      ↓
Draft PR / PR
      ↓
CI + 自检 + Review + 硬件证据
      ↓
Squash Merge
      ↓
删除分支，Issue 自动关闭
```

个人项目也应该对非微小修改使用 PR，以保留变更目的、验证结果和差异总览。允许直接提交 `main` 的例外必须非常有限，例如修正无争议的错别字；启用严格 Ruleset 后不再保留该例外。

### 10.2 同步与冲突

- 开始工作前从最新远端 `main` 建分支。
- PR 合并前应处理与 `main` 的冲突并重新运行检查。
- 个人短期分支可以 rebase 保持清晰；已经被多人共同使用的分支优先 merge `main` 或协调后再重写。
- 解决冲突时必须理解双方意图，禁止只按“ours/theirs”批量覆盖后直接提交。
- 生成文件冲突应优先合并配置源，再用指定版本的生成器重新生成。

### 10.3 Squash Merge

- 一个 PR 在 `main` 中形成一个 Commit。
- PR 标题必须符合 Conventional Commits，并成为 Squash Commit 标题。
- PR 描述保留设计、测试和硬件验证；不要求把所有信息塞进 Commit 标题。
- 合并后删除远端分支；本地无用分支应定期清理。

## 11. 高级 Git 操作判定表

高级命令的专业用法不是“经常使用”，而是知道何时安全、何时禁止。

| 操作 | 合适场景 | 关键边界 |
| --- | --- | --- |
| `commit --amend` | 修正尚未共享的最近 Commit | 已推送且有人基于它工作时不得擅自改写 |
| interactive rebase | 合并、拆分、排序个人分支 Commit | 禁止重写 `main` 和未协调的共享分支 |
| `revert` | 撤销已经共享或发布的 Commit | 默认保留历史，是公共历史回滚首选 |
| `reset` | 移动本地分支、调整暂存区 | `--hard` 会丢失工作区内容，执行前必须确认备份和目标 SHA |
| `cherry-pick` | 将明确修复 backport 到维护分支 | 会产生新 Commit；必须记录来源并避免重复合并 |
| `stash` | 临时切换上下文 | 不是长期存储；重要工作应形成分支和 Commit |
| `worktree` | 同时维护两个分支、紧急修复 | 一个分支不能同时被多个 worktree 检出 |
| `bisect` | 在已知 good/bad 之间定位回归 | 最好配合可自动判定的构建或测试 |
| `reflog` | 找回本地误删分支或 reset 前位置 | 只存在于本地且会过期，不能代替备份 |
| `clean` | 删除未追踪的可再生文件 | 高风险；先 dry-run，确认未追踪文件没有重要内容 |

### 11.1 Force Push

- 禁止对 `main`、发布分支和受保护 Tag 强制推送。
- 个人 PR 分支 rebase 后只能使用 `--force-with-lease`。
- 普通 `--force` 可能覆盖他人新提交，默认禁止。
- Force push 后必须重新检查 PR 差异和 CI 状态。

### 11.2 误操作恢复原则

1. 先停止继续修改，不要反复执行不确定的恢复命令。
2. 记录当前 `HEAD`、分支和 `git status`。
3. 已共享历史优先 `revert`，未共享历史才考虑 `reset/rebase`。
4. 使用 reflog 找回引用前，先创建临时恢复分支。
5. 凭据泄漏先轮换凭据，再处理 Git 历史；删除文件不是安全处置。

## 12. 版本、Tag 与 Changelog

### 12.1 版本兼容面

固件采用 SemVer 前必须声明其“公共接口”。嵌入式项目的公共接口可能包括：

- 通信协议和命令。
- Bootloader 与 Application 接口。
- 非易失存储布局和升级兼容性。
- 对外 C/C++ API。
- 配置文件、校准数据和模型格式。
- 支持的硬件修订版。

### 12.2 SemVer

```text
vMAJOR.MINOR.PATCH[-PRERELEASE][+BUILD]
```

| 变化 | 版本 |
| --- | --- |
| 不兼容的协议、存储或公开 API 变化 | MAJOR |
| 向后兼容的新功能 | MINOR |
| 向后兼容的 Bug 修复 | PATCH |
| 尚未承诺稳定的候选版本 | `-alpha.N`、`-beta.N`、`-rc.N` |

- `0.y.z` 表示接口仍处于早期开发，不等于可以不记录破坏性变化。
- 正式发布后不得移动 Tag 或替换同版本制品。
- 多固件仓库必须明确统一版本还是每个固件独立版本；不得临时混用。

### 12.3 Tag

- 正式版本使用 annotated tag；需要更高可信度时使用签名 Tag。
- Tag 必须指向通过发布检查的 Commit。
- Tag 名与 Release 版本完全一致，例如 `v1.2.0`。
- Tag 是 Git 历史标记；GitHub Release 是围绕 Tag 的发布说明和制品集合，两者不能混为一谈。

### 12.4 Changelog

```markdown
# Changelog

## [Unreleased]

### Added

### Changed

### Fixed

## [1.2.0] - 2026-07-17

### Added
- Add firmware update status reporting.
```

- Commit 面向开发者说明单次变更。
- Changelog 面向使用者汇总版本变化。
- Release Notes 面向某次交付，额外包含制品、硬件、升级和已知问题。

---

# 第三部分：GitHub 使用规范

## 13. 仓库基础设置

### 13.1 默认设置

使用 GitHub 的仓库应该：

- 将 `main` 设为默认分支。
- 启用 Squash Merge，并将其设为默认合并方式。
- 自动删除已合并的 head branch。
- 禁止 Wiki、Projects、Discussions 等未实际维护的功能，避免产生空入口。
- 设置准确的仓库描述、Topics 和可见性。

### 13.2 Rulesets 与 Branch Protection

新仓库优先使用 Rulesets；套餐或场景不支持时使用传统 Branch Protection 实现相同目标。

`main` 的推荐规则：

| 规则 | 个人基线 | 协作/开源 |
| --- | --- | --- |
| 禁止删除和 force push | 必须 | 必须 |
| 合并前通过 PR | 应该 | 必须 |
| 必需状态检查 | 有 CI 后必须 | 必须 |
| 所有对话已解决 | 应该 | 必须 |
| 线性历史 | 必须 | 必须 |
| 审批人数 | 0 | 至少 1，按风险提高 |
| 新提交后撤销旧审批 | 可以 | 高风险项目应该 |
| 管理员不得绕过 | 应该 | 应该 |

发布 Tag 规则应该禁止删除、更新和重建。只有发布自动化或明确的维护角色可以创建匹配的 Tag。

必需检查的 Job 名必须稳定且唯一；随意改名会导致 Ruleset 找不到检查或永久阻塞合并。

## 14. Issues

### 14.1 Issue 类型

| 类型 | 用途 | 最低内容 |
| --- | --- | --- |
| Bug | 已存在行为不符合预期 | 环境、版本、复现、实际/期望、日志 |
| Feature | 新能力或行为变化 | 问题、目标、验收条件、非目标 |
| Task | 工程维护或明确工作项 | 产出、完成条件、依赖 |
| Documentation | 文档缺失或错误 | 位置、读者、期望内容 |
| Question | 使用或设计讨论 | 背景和已查资料；公开社区优先 Discussion |
| Security | 潜在漏洞 | 不创建公开 Issue，按 `SECURITY.md` 私密报告 |

Issue 必须描述问题和完成条件，不要求在创建时就给出实现方案。

### 14.2 嵌入式 Bug 必要信息

- 固件版本或 Commit SHA。
- 芯片、板卡和硬件修订版。
- 构建配置、编译器/SDK/框架版本。
- 外设连接、供电和关键测试条件。
- 稳定复现步骤和复现概率。
- 串口日志、波形、错误码或最小证据。
- 期望行为与实际行为。
- 是否为回归，以及最后已知正常版本。

### 14.3 Labels

标签应少而稳定，建议使用命名空间：

```text
type:bug        type:feature       type:maintenance
area:firmware   area:hardware      area:tooling       area:docs
priority:p0     priority:p1        priority:p2         priority:p3
status:blocked  status:needs-info  status:ready
```

- Type 表示工作性质，Area 表示作用域，Priority 表示业务紧急程度，Status 表示流程状态。
- 不使用颜色或近义标签表达只有维护者自己知道的含义。
- 不把所有模块都预先建成标签；出现稳定筛选需求后再增加。

### 14.4 Milestones、Projects 与 Discussions

- Milestone 对应明确版本或交付节点，不用作永久 TODO 列表。
- GitHub Projects 用于跨 Issue/PR 的 Backlog、优先级、迭代和路线图；Issue 仍是具体工作的事实来源。
- Sub-issue 用于拆分可独立关闭的工作；Checklist 用于同一 Issue 内不可独立跟踪的小项。
- Discussion 用于开放问题、方案交流和用户问答；形成可执行结论后转为 Issue 或 ADR。

## 15. Pull Requests

### 15.1 PR 必要内容

每个 PR 必须说明：

- 为什么修改以及解决哪个 Issue。
- 主要行为和接口变化。
- 明确的非目标或未处理事项。
- 本地构建、自动测试和硬件验证结果。
- 对中断、时序、内存、功耗、协议、存储和兼容性的风险。
- 需要时提供回滚方式、日志、截图或测量结果。

### 15.2 PR 大小

- 一个 PR 只完成一个主要目标。
- 大型功能应按可独立验证的接口、基础能力和功能增量拆分。
- 不得把格式化、SDK 升级、大规模重命名和功能修改混在一起。
- 如果评审者无法在一次专注评审中理解差异，应优先拆分或先提交设计 ADR。

### 15.3 Draft PR

以下情况使用 Draft：

- 希望尽早展示接口和方向。
- CI 尚未全部通过或硬件验证未完成。
- 需要协作但尚不满足合并标准。

Draft 不是无限期备份。没有评审价值的本地实验应留在分支或 `spike/`。

### 15.4 Review

Review 应检查：

- 需求和验收条件是否满足。
- 架构边界、错误路径、并发、中断和资源生命周期。
- 是否存在静默失败、无界等待、越界、竞态或不可恢复状态。
- 测试是否真正覆盖风险而非只覆盖代码行。
- 文档、生成配置和发布兼容性是否同步。

评论分为：

- **Blocking**：合并前必须解决。
- **Suggestion**：建议改进，不阻塞当前目标。
- **Question**：需要解释以确认理解。
- **Nit**：极小风格问题，格式化工具能处理时不应人工反复争论。

个人项目没有独立 Reviewer 时，应完成 PR 模板中的自审清单，但不得伪造 Approval。

### 15.5 CODEOWNERS

团队项目可以使用 `.github/CODEOWNERS` 指定关键路径评审人，例如 bootloader、安全、硬件定义和发布流程。

- CODEOWNERS 只负责自动请求评审，不替代权限和 Ruleset。
- 规则从上到下匹配，后出现的规则可以覆盖前项；修改后必须验证实际匹配结果。
- 个人仓库没有真实代码所有者时不必创建。

## 16. Releases

### 16.1 Release 与 Artifact

| 对象 | 用途 | 生命周期 |
| --- | --- | --- |
| Actions Artifact | 保存某次 CI 的日志、测试结果和临时固件 | 有保留期限，可删除 |
| GitHub Release Asset | 向使用者交付正式或预发布制品 | 跟随 Release 长期保留 |
| Git Tag | 标记源码历史中的版本 Commit | 不包含发布说明和额外制品 |

不得把 CI Artifact 链接当作长期发布地址。

### 16.2 固件 Release 内容

正式 Release 应包含：

- 版本、发布日期和对应 Tag/Commit。
- 支持的芯片、板卡和硬件修订版。
- 构建配置、工具链、SDK 或框架版本。
- `.bin`、`.hex`、必要时 `.elf`、`.map` 和调试符号。
- SHA-256 等校验和。
- 烧录地址、升级前置条件和配置迁移说明。
- 新功能、修复、破坏性变化、已知问题和回滚方式。

### 16.3 发布纪律

- 发布制品必须由受控 CI 从 Tag 对应 Commit 重新构建，不直接上传开发者本机未追踪的文件。
- 发布前先创建 Draft、收集制品和说明，再正式发布。
- 预发布必须标记 prerelease，不能与稳定通道混淆。
- 已发布版本的 Tag 和制品不得替换；修正必须发布新版本。
- 支持时应该启用不可变 Release、制品证明或签名，提高供应链可信度。

## 17. GitHub Actions

### 17.1 CI 质量门

通用嵌入式 CI 按项目能力逐级启用：

1. **仓库检查**：格式、Markdown、YAML、提交/PR 标题。
2. **静态分析**：编译警告、clang-tidy/cppcheck 或项目选择的工具。
3. **主机测试**：算法、协议、状态机和序列化。
4. **构建矩阵**：Debug/Release、板卡、固件或编译器组合。
5. **资源预算**：Flash、RAM、栈和二进制大小阈值。
6. **集成/HIL**：烧录、启动、自检、通信和真实硬件行为。
7. **Release**：Tag 校验、可复现构建、校验和和制品发布。

厂商代码的警告不能简单用全局关闭掩盖。应将自有代码和无法控制的上游代码分开配置。

### 17.2 触发规则

- PR：运行合并所需的快速且确定性检查。
- Push 到 `main`：运行完整软件检查并保存候选制品。
- Tag：运行发布构建，不复用来源不明的本地二进制。
- `workflow_dispatch`：用于受控手动任务，不得成为绕过正常检查的后门。
- Schedule：适合长时间测试、依赖审计和硬件巡检。

不得对来自不可信 Fork 的代码使用 `pull_request_target` 检出并执行其内容。需要处理标签或评论时，应让高权限工作流不执行 PR 中的代码。

### 17.3 权限与第三方 Action

- 工作流顶层默认 `permissions: contents: read`。
- 只有发布 Job 临时获得 `contents: write`，证明 Job 获得其必需权限。
- 第三方 Action 应固定到完整 Commit SHA；版本标签只用于说明可读版本。
- 使用 Dependabot 更新 `github-actions` 依赖，避免固定 SHA 永久不升级。
- 不把 Secrets 打印到日志，不把秘密写入 Artifact 或缓存。
- 来自 Fork 的 PR 默认拿不到 Secrets，不应为通过测试而放宽这一安全边界。

### 17.4 Cache 与 Artifact

- Cache 用于可重新下载或生成的依赖和中间数据，命中失败不得影响正确性。
- Artifact 用于保存构建输出、日志和测试报告。
- Cache key 必须包含依赖锁定信息、工具链和必要的目标配置。
- Artifact 名必须包含固件、板卡和构建类型，不能全部叫 `firmware`。
- 为 Artifact 设置合理保留期，正式交付转入 Release。

### 17.5 Concurrency

PR 工作流应该按分支或 PR 编号设置 concurrency，新提交到来时取消旧的未完成运行，节省资源并避免过期结果干扰。

发布和真实硬件任务不能随意取消；它们应使用独立 concurrency group，保证同一目标设备同一时间只有一个烧录/测试 Job。

### 17.6 HIL 与自托管 Runner

HIL 属于高级能力，满足以下条件后启用：

- 测试台、目标板、调试器和供电控制有固定标识。
- 可以自动复位、烧录、收集日志、超时和恢复。
- 测试有机器可判定的结果。
- Runner 不执行不可信 PR 代码。
- Runner 凭据权限最小，并有隔离、清理和升级流程。

推荐将 HIL 分成：

- PR 冒烟测试：短、稳定、覆盖启动和关键接口。
- 主干回归：更完整的设备和功能组合。
- 夜间耐久测试：长时间、功耗、内存泄漏或通信压力。

真实设备故障、测试基础设施故障和产品测试失败必须能区分，避免把所有失败都报告为“代码错误”。

### 17.7 Environments 与部署通道

GitHub Environment 是部署权限和秘密的边界，不是另一个 Git 分支。嵌入式项目可以按实际流程使用：

- `hil-lab`：访问测试台和设备凭据。
- `release-signing`：访问签名密钥并要求人工批准。
- `beta` / `production`：OTA、设备群或正式交付通道。

规则：

- 只有确实存在受控设备、签名或部署动作时才创建 Environment。
- 正式环境应该限制允许部署的 Branch/Tag，并按风险增加审批。
- 同一固件进入不同通道应引用同一已验证制品，不应在每个环境重新编译出不同二进制。
- 部署记录必须包含版本、制品校验和、目标设备/批次、操作者和结果。
- 支持远程更新时，应先小范围验证再逐步扩大，并保留可验证的回滚路径。

## 18. GitHub 安全与依赖治理

### 18.1 Secrets

- 启用 Secret Scanning 和 Push Protection（仓库/套餐支持时）。
- 固件中不得硬编码生产密钥；测试密钥必须明确标记且不能用于生产。
- GitHub Secrets 按 repository、environment 或 organization 的最小范围配置。
- 环境 Secrets 只有通过对应 Environment 保护规则后才能访问。
- 怀疑泄漏时立即轮换，不等待历史清理完成。

### 18.2 Dependabot 与依赖审查

- 对 GitHub Actions、Python 包和项目实际使用的包生态启用版本更新。
- 启用 Dependency Review 后，PR 应显示新增依赖、许可证和已知漏洞变化。
- 自动更新 PR 仍必须通过 CI，不得无条件自动合并重大版本升级。
- 对无法被 GitHub 识别的厂商 SDK，在 `third_party` 清单或 SBOM 中手工记录版本。

### 18.3 Code Scanning

- 公开或高风险 C/C++、Python 项目应该启用 CodeQL 或等效扫描。
- 编译型嵌入式工程应选择能正确看见实际源码和编译宏的构建模式。
- 扫描告警必须分类为修复、误报或接受风险，并留下依据；禁止仅为“绿色状态”全局忽略规则。

### 18.4 SBOM、签名和制品证明

对外发布或供应链要求较高的项目应该：

- 生成软件物料清单，记录第三方组件、版本和许可证。
- 为 Release 制品生成校验和。
- 在工具链允许时生成签名、来源证明或 Artifact Attestation。
- 将签名私钥放在受保护的发布环境，不放在普通仓库 Secret 或自托管开发机。

## 19. 其他 GitHub 功能的使用边界

| 功能 | 适合场景 | 不适合场景 |
| --- | --- | --- |
| Projects | 多 Issue、路线图、跨仓库计划 | 只有几个简单 TODO |
| Discussions | 用户问答、开放设计讨论、公告 | 明确可执行的 Bug/Task |
| Pages | Doxygen、协议文档、用户手册 | 只复制 README |
| Packages | 发布可复用库、容器或工具包 | 单个裸机固件二进制 |
| Wiki | 非版本化社区知识、编辑门槛需要很低 | 必须与源码同步评审的工程文档 |

工程设计文档默认放在仓库 `docs/` 中随 PR 评审，而不是放 Wiki。

---

# 第四部分：标准工作场景

## 20. 普通功能开发

1. 创建 Feature Issue，写明目标、非目标和验收条件。
2. 从最新 `main` 创建 `feat/...` 分支。
3. 先确定接口和测试边界，再分阶段实现。
4. 提交原子 Commit；功能、直接测试和必要文档保持同步。
5. 尽早创建 Draft PR 暴露设计方向。
6. 完成本地检查、CI 和必要硬件验证。
7. 更新 PR 说明、Changelog（若用户可见）和文档。
8. 通过 Review 后 Squash Merge，删除分支并关闭 Issue。

## 21. Bug 修复

1. Issue 记录版本、硬件、复现和最后正常版本。
2. 从应修复的主线创建 `fix/...`。
3. 优先增加能够失败的复现测试或确定性复现脚本。
4. 修复根因，验证没有只遮蔽症状。
5. 检查相邻错误路径、回归范围和兼容影响。
6. PR 说明根因、验证证据和是否需要 backport。
7. 合并后由新版本发布，不修改旧 Release。

## 22. 独立测试或实验

### 独立测试基础设施

- 使用 `test/...` 分支。
- PR 必须说明测试对象、环境、通过条件和误报处理。
- 测试平台变更不能悄悄降低原有质量门。

### 风险实验

- 使用 `spike/...` 分支。
- 允许临时代码，但不得包含真实凭据或无法合法分发的文件。
- 给实验设置时间和结论出口。
- 成功后重新整理为正式 PR；失败后在 Issue/ADR 记录原因并删除分支。

## 23. 正式发布

1. 确认目标 Issue/Milestone 完成，`main` 全部检查通过。
2. 确定版本号并更新 Changelog、版本信息和迁移说明。
3. 验证支持硬件、升级路径、回滚和关键 HIL 用例。
4. 创建受保护 Tag 触发 Release 工作流。
5. CI 从 Tag 重新构建全部正式配置。
6. 生成固件、map、校验和、SBOM/证明（启用时）。
7. 创建 Draft Release，核对版本、制品、硬件和说明。
8. 发布稳定版或 prerelease；不可变发布启用后不得替换资产。
9. 发布失败时修复并创建新版本或新候选版本，不移动已公开 Tag。

## 24. 紧急回滚

- 首先确定是停止发布、回滚部署还是回滚源码，三者不是同一动作。
- 已发布固件存在严重问题时，应下架推荐入口或明确标记风险，但保留审计信息。
- 使用 `revert` 在受支持分支形成可审查 PR。
- 修复或回滚仍需最小 CI/HIL 验证。
- 发布新的 PATCH 版本并说明受影响版本、恢复步骤和数据兼容风险。

---

# 第五部分：检查清单

## 25. 新仓库检查清单

- [ ] README 能让新环境完成最小构建和验证。
- [ ] 构建入口支持非交互执行。
- [ ] 编译器、SDK、框架和生成器版本已记录。
- [ ] `.gitignore` 排除构建结果和秘密，但保留必要配置源。
- [ ] `.gitattributes` 规定行尾和二进制类型。
- [ ] 源码、生成代码、第三方代码和构建输出边界清晰。
- [ ] 第三方依赖有来源、版本和许可证。
- [ ] `main` 是默认健康分支。
- [ ] Commit、分支、PR 和版本规范已写入贡献说明或引用本文。
- [ ] GitHub 仓库已设置合并方式和分支保护。
- [ ] 至少有构建 CI；可测试逻辑逐步增加主机测试。
- [ ] 公开/交付项目已有 LICENSE、CHANGELOG 和 SECURITY。

## 26. 日常 Commit 检查清单

- [ ] 本次修改只有一个主要目的。
- [ ] 已检查暂存区，而不只是工作区。
- [ ] 没有密钥、私人路径、日志或意外大文件。
- [ ] 生成配置与生成结果一致。
- [ ] 已执行与风险相称的构建和测试。
- [ ] Commit Type、Scope、英文 Subject 正确。
- [ ] 行为变化、原因和 Issue 关联已在 body/footer 中说明。

## 27. PR 检查清单

- [ ] 标题符合 Conventional Commits。
- [ ] 关联 Issue，并说明目标和非目标。
- [ ] 差异不混入无关格式化、重命名或依赖升级。
- [ ] CI 全部通过，失败/跳过项有解释。
- [ ] 记录主机测试、硬件、板卡和构建配置。
- [ ] 检查中断、并发、超时、错误恢复和资源生命周期。
- [ ] 检查 Flash/RAM/栈、时序、功耗或协议影响（适用时）。
- [ ] README、协议、配置、Changelog 已同步（适用时）。
- [ ] Review 对话已解决，Squash 标题准确。

## 28. Release 检查清单

- [ ] 版本与兼容性变化匹配。
- [ ] Tag 指向已通过检查的 Commit。
- [ ] Release 制品由 CI 从 Tag 构建。
- [ ] 文件名包含产品、板卡、版本和必要构建信息。
- [ ] 提供校验和、工具链版本和烧录/升级说明。
- [ ] 支持硬件和硬件修订版明确。
- [ ] Changelog、Release Notes 和已知问题完整。
- [ ] 已验证升级、配置/存储迁移和回滚路径。
- [ ] 稳定版与 prerelease 标记正确。
- [ ] 发布后不再移动 Tag 或覆盖制品。

## 29. HIL 检查清单

- [ ] Runner 不执行不可信 Fork PR 代码。
- [ ] 目标板、调试器、串口和电源可唯一识别。
- [ ] 烧录、复位、超时和日志收集自动化。
- [ ] 每个用例有机器可判定结果。
- [ ] 失败能区分产品、设备和基础设施问题。
- [ ] Runner 权限、凭据、网络和更新受控。
- [ ] 同一设备通过 concurrency 防止并发占用。

## 30. 误操作检查清单

- [ ] 已停止继续执行不确定命令。
- [ ] 已记录 `status`、当前分支和 `HEAD`。
- [ ] 已判断历史是否共享或发布。
- [ ] 共享历史优先使用 `revert`。
- [ ] 使用 reflog 恢复前已建立临时分支。
- [ ] 执行 `reset --hard`、`clean` 或 force push 前已确认不可恢复影响。
- [ ] 涉及秘密时已先轮换凭据。

---

# 附录 A：通用 README 模板

````markdown
# <Project Name>

<一句话说明项目解决的问题。>

> Status: prototype / active / stable / archived

## Features

- <Feature 1>
- <Feature 2>

## Supported targets

| Target | Board revision | Status | Notes |
| --- | --- | --- | --- |
| <MCU/board> | <revision> | supported | <constraints> |

## Hardware

- Controller: <part number>
- Debugger/programmer: <tool>
- Required peripherals: <list>
- Pinout and schematic: [hardware documentation](docs/hardware.md)

## Toolchain

| Tool | Version |
| --- | --- |
| Compiler/framework | <version> |
| SDK/generator | <version> |
| Build tool | <version> |

## Quick start

### Prerequisites

<Dependency installation or official environment link.>

### Configure

```sh
<configure command>
```

### Build

```sh
<build command>
```

Output: `<artifact path>`

### Flash and verify

```sh
<flash command>
<verification command>
```

Expected result: <deterministic observable result>.

## Project structure

```text
<actual repository tree and one-line responsibilities>
```

## Architecture

<Short overview and link to docs/architecture.md.>

## Testing

```sh
<local check command>
```

Document hardware test conditions and supported targets.

## Configuration

<Build variants, non-secret configuration and environment template.>

## Releases

See [CHANGELOG.md](CHANGELOG.md) and the repository Releases page.

## Known limitations

- <Limitation>

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

<License and third-party notice.>
````

---

# 附录 B：Issue Forms

## B.1 Bug Report

保存为 `.github/ISSUE_TEMPLATE/bug.yml`，替换尖括号占位内容：

```yaml
name: Bug report
description: Report reproducible incorrect behavior
title: "[Bug]: "
labels:
  - "type:bug"
body:
  - type: markdown
    attributes:
      value: |
        Do not report security vulnerabilities here. Follow SECURITY.md.

  - type: textarea
    id: summary
    attributes:
      label: Summary
      description: Describe the incorrect behavior briefly.
    validations:
      required: true

  - type: input
    id: version
    attributes:
      label: Firmware version or commit
      placeholder: v1.2.0 or commit SHA
    validations:
      required: true

  - type: input
    id: hardware
    attributes:
      label: Target hardware
      placeholder: MCU, board, and hardware revision
    validations:
      required: true

  - type: textarea
    id: environment
    attributes:
      label: Build and test environment
      description: Build configuration, compiler, SDK/framework, peripherals, and power conditions.
    validations:
      required: true

  - type: textarea
    id: reproduce
    attributes:
      label: Steps to reproduce
      placeholder: |
        1. ...
        2. ...
        3. ...
    validations:
      required: true

  - type: textarea
    id: expected
    attributes:
      label: Expected behavior
    validations:
      required: true

  - type: textarea
    id: actual
    attributes:
      label: Actual behavior and evidence
      description: Add logs, error codes, waveforms, and reproduction rate. Remove secrets.
    validations:
      required: true

  - type: input
    id: last_known_good
    attributes:
      label: Last known good version
      placeholder: Unknown, version, or commit SHA

  - type: checkboxes
    id: checks
    attributes:
      label: Checklist
      options:
        - label: I searched for duplicate issues.
          required: true
        - label: I removed credentials and private data from the evidence.
          required: true
```

模板引用的 `type:bug` 标签必须先在仓库创建；如果不采用该标签体系，应删除或改成真实标签。Feature 模板中的 `type:feature` 同理。

## B.2 Feature Request

保存为 `.github/ISSUE_TEMPLATE/feature.yml`：

```yaml
name: Feature request
description: Propose a new capability or behavior change
title: "[Feature]: "
labels:
  - "type:feature"
body:
  - type: textarea
    id: problem
    attributes:
      label: Problem
      description: What problem should be solved? Avoid starting with an implementation.
    validations:
      required: true

  - type: textarea
    id: goal
    attributes:
      label: Goal and acceptance criteria
      placeholder: |
        - [ ] Observable result 1
        - [ ] Observable result 2
    validations:
      required: true

  - type: textarea
    id: constraints
    attributes:
      label: Constraints
      description: Hardware, timing, memory, power, protocol, and compatibility constraints.

  - type: textarea
    id: non_goals
    attributes:
      label: Non-goals
      description: Explicitly state what this request will not address.

  - type: textarea
    id: context
    attributes:
      label: Additional context
```

## B.3 Issue 配置

保存为 `.github/ISSUE_TEMPLATE/config.yml`：

```yaml
blank_issues_enabled: false
contact_links:
  - name: Security vulnerability
    url: <SECURITY_REPORT_URL>
    about: Report security vulnerabilities privately.
  - name: Questions and support
    url: <DISCUSSION_OR_SUPPORT_URL>
    about: Ask usage questions outside the bug tracker.
```

---

# 附录 C：Pull Request 模板

保存为 `.github/PULL_REQUEST_TEMPLATE.md`：

```markdown
## Summary

<!-- What changed and why? -->

Closes: #<issue>

## Changes

- 

## Non-goals

- 

## Validation

### Automated

- [ ] Formatting/static checks
- [ ] Unit/integration tests
- [ ] Required build configurations

### Hardware

| Target | Revision | Build | Result |
| --- | --- | --- | --- |
| N/A | N/A | N/A | Not required |

Evidence:

## Risk and compatibility

- Interrupt/concurrency:
- Flash/RAM/stack:
- Timing/power:
- Protocol/storage/configuration:
- Rollback:

## Checklist

- [ ] The PR has one primary goal.
- [ ] The title follows Conventional Commits.
- [ ] Generated files match their configuration source.
- [ ] No secrets, private paths, build output, or accidental binaries were added.
- [ ] Documentation and changelog were updated when required.
```

---

# 附录 D：通用 CI 工作流骨架

保存为 `.github/workflows/ci.yml`。模板中的命令和 Action SHA 必须替换后才能启用；不得直接保留占位符。

```yaml
name: CI

on:
  pull_request:
    branches:
      - main
  push:
    branches:
      - main
  workflow_dispatch:

permissions:
  contents: read

concurrency:
  group: ci-${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true

jobs:
  check:
    name: check
    runs-on: ubuntu-latest
    timeout-minutes: 15
    steps:
      - name: Check out repository
        uses: actions/checkout@<PINNED_FULL_COMMIT_SHA>

      - name: Install tools
        run: |
          <INSTALL_CHECK_TOOLS>

      - name: Run formatting and static checks
        run: |
          <RUN_CHECKS>

  host-tests:
    name: host-tests
    runs-on: ubuntu-latest
    timeout-minutes: 20
    steps:
      - name: Check out repository
        uses: actions/checkout@<PINNED_FULL_COMMIT_SHA>

      - name: Install test dependencies
        run: |
          <INSTALL_TEST_DEPENDENCIES>

      - name: Run host tests
        run: |
          <RUN_HOST_TESTS>

  build:
    name: build-${{ matrix.target }}-${{ matrix.config }}
    runs-on: ubuntu-latest
    timeout-minutes: 30
    strategy:
      fail-fast: false
      matrix:
        target:
          - <TARGET_A>
        config:
          - Debug
          - Release
    steps:
      - name: Check out repository
        uses: actions/checkout@<PINNED_FULL_COMMIT_SHA>

      - name: Install toolchain
        run: |
          <INSTALL_TOOLCHAIN>

      - name: Build firmware
        run: |
          <BUILD_COMMAND_USING_MATRIX_TARGET_AND_CONFIG>

      - name: Check resource budgets
        run: |
          <CHECK_FLASH_RAM_AND_SIZE_BUDGETS>

      - name: Upload firmware artifacts
        uses: actions/upload-artifact@<PINNED_FULL_COMMIT_SHA>
        with:
          name: firmware-${{ matrix.target }}-${{ matrix.config }}-${{ github.sha }}
          path: |
            <ARTIFACT_PATHS>
          if-no-files-found: error
          retention-days: 14
```

要求：

- Ruleset 只要求稳定 Job 名，例如 `check`、`host-tests` 和全部规定的 build matrix。
- 工具链下载必须校验版本和来源；频繁下载可以使用 Cache，但 Cache 不能成为唯一副本。
- 如果项目没有主机测试，先移除对应 Job，不得用永远成功的占位命令伪造质量门。

---

# 附录 E：Release 工作流骨架

保存为 `.github/workflows/release.yml`。正式使用前必须替换占位符并限制 Tag 创建权限。

```yaml
name: Release

on:
  push:
    tags:
      - "v*.*.*"

permissions:
  contents: read

jobs:
  build-release:
    name: build-release-${{ matrix.target }}
    runs-on: ubuntu-latest
    timeout-minutes: 40
    strategy:
      fail-fast: false
      matrix:
        target:
          - <TARGET_A>
    steps:
      - name: Check out tagged source
        uses: actions/checkout@<PINNED_FULL_COMMIT_SHA>

      - name: Validate tag and project version
        run: |
          <VALIDATE_TAG_MATCHES_PROJECT_VERSION>

      - name: Install release toolchain
        run: |
          <INSTALL_PINNED_TOOLCHAIN>

      - name: Build and test release
        run: |
          <BUILD_RELEASE_FOR_TARGET>
          <RUN_RELEASE_TESTS>

      - name: Package and generate checksums
        run: |
          <PACKAGE_BIN_HEX_ELF_MAP_AND_METADATA>
          sha256sum <RELEASE_FILES> > "${{ matrix.target }}-SHA256SUMS.txt"

      - name: Upload release bundle
        uses: actions/upload-artifact@<PINNED_FULL_COMMIT_SHA>
        with:
          name: release-${{ matrix.target }}-${{ github.ref_name }}
          path: |
            <RELEASE_BUNDLE_PATH>
            ${{ matrix.target }}-SHA256SUMS.txt
          if-no-files-found: error
          retention-days: 30

  publish:
    name: publish-release
    needs:
      - build-release
    runs-on: ubuntu-latest
    permissions:
      contents: write
    steps:
      - name: Download release bundles
        uses: actions/download-artifact@<PINNED_FULL_COMMIT_SHA>
        with:
          pattern: release-*
          path: release-assets
          merge-multiple: true

      - name: Publish GitHub release
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          mapfile -d '' assets < <(find release-assets -type f -print0)
          if (( ${#assets[@]} == 0 )); then
            echo "No release assets were downloaded" >&2
            exit 1
          fi
          gh release create "${GITHUB_REF_NAME}" "${assets[@]}" \
            --verify-tag \
            --generate-notes \
            --draft
```

工作流默认创建 Draft Release，维护者核对硬件、制品和说明后再发布。项目可以在流程成熟后改为自动发布 prerelease 或稳定版。

---

# 附录 F：Dependabot 与 Release Notes

## F.1 Dependabot

保存为 `.github/dependabot.yml`，只保留项目实际使用的生态：

```yaml
version: 2
updates:
  - package-ecosystem: github-actions
    directory: "/"
    schedule:
      interval: monthly
    labels:
      - "area:ci"
      - "type:maintenance"
    open-pull-requests-limit: 5

  - package-ecosystem: pip
    directory: "/tools"
    schedule:
      interval: monthly
    labels:
      - "area:tooling"
      - "type:maintenance"
    open-pull-requests-limit: 5
```

未使用 Python 或依赖文件不在 `/tools` 时必须删除或修改对应项。厂商 SDK 若不属于 Dependabot 支持的包生态，应由 Issue、定期检查或依赖清单维护。

## F.2 自动 Release Notes 分类

保存为 `.github/release.yml`：

```yaml
changelog:
  exclude:
    labels:
      - "skip-changelog"
  categories:
    - title: Features
      labels:
        - "type:feature"
    - title: Fixes
      labels:
        - "type:bug"
    - title: Documentation
      labels:
        - "area:docs"
    - title: Maintenance
      labels:
        - "type:maintenance"
    - title: Other changes
      labels:
        - "*"
```

自动说明是 Release Notes 草稿，不替代人工维护的兼容性、硬件、升级和已知问题说明。

---

# 附录 G：推荐 Ruleset 配置表

| 项目 | `main` | `v*` Tags |
| --- | --- | --- |
| Target | Default branch | Release tags |
| Enforcement | Active | Active |
| Restrict deletion | 开启 | 开启 |
| Block force pushes | 开启 | 开启 |
| Require pull request | 开启 | 不适用 |
| Required approvals | 个人 0；团队至少 1 | 不适用 |
| Dismiss stale approvals | 高风险团队开启 | 不适用 |
| Require status checks | `check`、测试、规定构建矩阵 | 发布工作流负责验证 |
| Require conversation resolution | 开启 | 不适用 |
| Require linear history | 开启 | 不适用 |
| Restrict updates | 不允许直接更新 | 发布角色/自动化以外禁止 |
| Bypass | 最小化并记录 | 最小化并记录 |

Ruleset 应先以 Evaluate（如果账户和仓库支持）观察，再转为 Active；启用必需检查前，必须至少成功运行一次对应 Workflow，确认 Job 名准确。

---

# 附录 H：权威资料

规范中的平台能力和语法应以以下资料为准：

- [Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/)
- [Semantic Versioning 2.0.0](https://semver.org/)
- [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/)
- [Git 官方工作流文档](https://git-scm.com/docs/gitworkflows)
- [GitHub：About rulesets](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-rulesets/about-rulesets)
- [GitHub：About merge methods](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/configuring-pull-request-merges/about-merge-methods-on-github)
- [GitHub：Issue and pull request templates](https://docs.github.com/en/communities/using-templates-to-encourage-useful-issues-and-pull-requests)
- [GitHub：About releases](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)
- [GitHub：Workflow artifacts](https://docs.github.com/en/actions/concepts/workflows-and-actions/workflow-artifacts)
- [GitHub Actions：Secure use reference](https://docs.github.com/en/actions/reference/security/secure-use)
- [GitHub：Self-hosted runners](https://docs.github.com/en/actions/reference/runners/self-hosted-runners)
- [GitHub：Code security quickstart](https://docs.github.com/en/code-security/getting-started/quickstart-for-securing-your-repository)
- [GitHub：Community health files](https://docs.github.com/en/communities/setting-up-your-project-for-healthy-contributions)

GitHub 的功能、套餐范围和界面会变化。维护本文时应重新核对官方文档，并更新首页的复核日期。
