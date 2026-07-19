# 嵌入式竞赛与紧急冲刺开发流程

> 版本：1.0
> 适用周期：约 1～7 天
> 适用场景：嵌入式比赛、限时演示、紧急原型和短周期交付
> 详细规范：[Development_Workflow.md](Development_Workflow.md)

本文是开发者和 Agent 的快速模式执行手册。它只保留能够保护可运行版本、支持快速试错并保证最终交付的规则；目录、Git、GitHub、Release 等完整说明统一查阅通用规范。

---

## 0. 使用原则

快速模式只追求四个结果：

1. 始终保留一个已验证、可运行的版本。
2. 高风险尝试失败后能够立即退回。
3. 换电脑、休息或交接后仍能继续构建。
4. 截止前能够交付源码、固件和必要说明。

满足以下条件时使用本文：

- 截止时间明确，周期通常不超过一周。
- 功能闭环和现场可靠性优先于长期仓库治理。
- 项目结束后允许补充文档、测试和工程化设施。

没有明确截止时间的普通项目直接使用通用规范。项目需要长期维护、多人持续协作、正式发布、远程更新或安全治理时，必须退出快速模式。

### 0.1 规则裁剪

| 必须执行 | 已经稳定就继续使用 | 可以推迟 |
| --- | --- | --- |
| Git 和 `.gitignore` | 一键构建/烧录脚本 | 完整目录重构 |
| 厂商配置源和构建配置 | 现成 CI | 完整 Issue/Projects 体系 |
| 最小 README 和 DEVLOG | 格式化、静态分析 | 每个功能都走远端 PR |
| 阶段性 Commit 和可运行 Tag | 主机测试、现成 HIL | CodeQL、Dependabot、SBOM |
| 编译、烧录和核心路径测试 | 团队已熟悉的 Review | 从零建设自动 HIL/发布系统 |
| 远端源码和独立制品备份 | 已有 Release 自动化 | 完整 Changelog/Release Notes |

秘密信息、许可证、危险 Git 操作和已共享历史规则不得降级。不要为了“专业”在冲刺中临时引入陌生框架或工具。

---

## 1. 启动阶段：前 5%～10% 时间

一个四天三夜的项目，应在最初约 30～90 分钟完成本节。

### 1.1 建立基线

```text
取得源码 → 指定工具链构建 → 烧录 → 观察确定结果 → Commit + Tag
```

确定结果可以是 LED 节奏、串口版本、显示内容或最小自检。“烧录成功”本身不能证明固件已经正常启动。

首次可运行版本标记为：

```text
checkpoint/baseline
```

### 1.2 保存可复现输入

必须保存并记录：

- `.ioc`、`.syscfg`、板卡配置等生成器配置源。
- 构建配置、链接脚本、自有源码和必要生成源码。
- 编译器、IDE、SDK、框架和生成器版本。
- 构建、烧录方式和产物位置。

不得为了套用通用目录树而移动厂商生成文件，也不要在冲刺开始时升级工具链。

### 1.3 建立两个最小文档

README 负责回答：

- 使用什么硬件和工具版本。
- 如何构建、烧录和判断启动成功。
- 关键引脚、总线地址和协议在哪里。
- 当前可以工作的功能是什么。

`DEVLOG.md` 是唯一冲刺任务入口，记录：

- 当前最高优先级和最后已知正常 Tag。
- Must/Should/Optional 任务。
- 阻塞问题、临时规避、关键参数和测试结果。

模板见附录。聊天记录和脑内记忆不能成为唯一事实来源。

### 1.4 建立备份

- 将源码和 Tag 推送到远端。
- 将可烧录基线固件复制到仓库之外。
- 条件允许时准备第二台可构建/烧录的电脑或离线工具链。

---

## 2. 开发阶段：中间 65%～75% 时间

### 2.1 单人模式

```text
main       最近一次已经验证的集成状态
spike/*    可能破坏系统的短期实验
```

普通修改进入 `main` 前至少完成：

```text
编译通过 → 烧录成功 → 相关功能冒烟测试 → Commit
```

下列修改先进入 `spike/`：

- 重写主状态机、调度器或通信协议。
- 大改时钟、生成器配置、启动流程或内存布局。
- 更换 SDK、框架、编译器、算法或核心依赖。
- 同时影响多个已经工作的核心模块。

实验成功后，只合回整理过且可验证的结果；失败则记录结论并删除分支。

### 2.2 团队模式

- 每个人或独立功能使用短期分支，指定一名集成人维护 `main`。
- 生成器配置、引脚、时钟和链接脚本尽量由一人集中修改。
- 合并前由另一人查看差异，并确认构建与硬件结果。
- 每天至少集成一次，禁止截止前才统一合并。
- 接口和数据格式先写入 DEVLOG/文档，再由两端并行实现。

已有 PR/CI 就继续使用；没有时可以快速 Review，不必临时搭建完整 GitHub 流程。

### 2.3 Commit

继续使用英文 Conventional Commits，冲刺期间可以只写清晰标题：

```text
feat(motor): add encoder speed control
fix(sensor): handle invalid i2c response
feat(control): add junction state machine
chore(board): update timer configuration
```

以下时刻必须提交：

- 一个模块或完整功能首次稳定工作。
- 准备进行高风险修改。
- 参数达到可复现的可用状态。
- 准备休息、换电脑、换人或离开现场。

按可运行增量提交，不按固定时间机械提交，也不能把全部工作压成 `final update`。完整规则见通用规范 §8～§11。

### 2.4 里程碑 Tag

```text
checkpoint/board-ready
checkpoint/drivers-ready
checkpoint/basic-route
checkpoint/full-run
```

只有确实可运行的版本才能打 checkpoint。DEVLOG 必须记录对应硬件、构建配置、通过的测试、已知问题和固件位置。

### 2.5 功能优先级

1. **Must**：缺失后无法完成基本比赛或演示。
2. **Reliability**：修复死机、失控、无法启动和高概率失败。
3. **Should**：明显提高得分或效果，但不破坏基本路径。
4. **Optional**：优化、低概率边缘情况、外观和整理。

Must 尚未闭环时，不投入大量时间美化目录、日志、动画或低价值细节。

### 2.6 参数调试

控制、视觉、阈值和时序参数至少记录名称、单位、测试条件、当前值、最后正常值和修改结果。达到稳定状态后再 Commit/Tag，不保留一串无法解释的调参提交。

---

## 3. 最小测试与功能冻结

### 3.1 五层验证

| 层级 | 内容 | 执行时机 |
| --- | --- | --- |
| Build | 指定配置能够构建 | 每次 Commit 前 |
| Boot | 烧录、复位、心跳/版本正常 | 修改底层或集成后 |
| Module smoke | 涉及的外设和模块基本工作 | 每个功能增量 |
| Critical path | 完整执行最关键路线 | 每个主要里程碑 |
| Full rehearsal | 按现场条件从上电执行到结束 | 功能冻结后至少两次 |

人工测试必须记录为“通过、失败、未测”，不能只写“基本正常”。按项目风险检查冷启动、重复上电、供电负载、通信超时、错误帧、DMA/中断、缓冲区、看门狗、安全状态、Flash/RAM 和现场环境变化。

一次失败至少记录 Commit/Tag、硬件、复现步骤、概率、证据、规避方法以及是否阻塞提交。

### 3.2 最后 20%～25% 功能冻结

冻结后禁止：

- 升级工具链、SDK、框架或生成器。
- 无必要地重新生成全部工程。
- 大规模格式化、重命名、清理或目录重构。
- 更换已经可用的算法、协议或调度架构。
- 加入没有通过完整路线测试的新功能。

冻结后只允许修复阻塞问题、调整有回退值的必要参数、补充诊断与测试，以及回退 checkpoint。

最终演练必须从断电开始，使用最终硬件、固件和配置，按真实顺序完整执行至少两次。

---

## 4. 故障、提交与备份

### 4.1 最后阶段严重故障

```text
停止新增功能
  ↓
确认故障版本
  ↓
恢复最近已验证 checkpoint
  ↓
确认基本路径
  ↓
只重放必要修复
  ↓
重新完整演练
```

少功能但稳定的版本，通常优于功能齐全却无法完成流程的版本。

Git 误操作时先停止，记录分支、`HEAD` 和工作区状态；不要继续执行 `reset --hard`、`clean` 或 force push。共享历史优先 `revert`，详细恢复方式见通用规范 §11。

### 4.2 最终 Tag

```text
submission/<event>-final
```

重新提交时不得移动原 Tag，应创建 `submission/<event>-final-2`。转入长期维护后，再按通用规范发布 SemVer 版本。

### 4.3 最终交付包

```text
submission/
├── source/                 # 源码快照或仓库+Commit信息
├── firmware/               # bin/hex及必要elf/map
├── README.md               # 硬件、工具链、烧录和验证
├── VERSION.txt             # Commit、Tag、时间和目标硬件
└── SHA256SUMS.txt          # 制品校验和
```

交付包不一定进入源码 Git，但必须保存到至少两个独立位置。最终核对：

- Tag 与实际烧录固件来自同一 Commit。
- 文件名能够区分目标板、版本和构建配置。
- README 写明烧录地址、顺序和必要配置。
- 已删除真实秘密、个人路径和禁止分发的资料。
- 使用交付包中的固件重新烧录或校验一次。

---

## 5. Agent 快速模式约束

### 5.1 读取顺序

1. 读取本文、项目 README、DEVLOG 和仓库状态。
2. 确认剩余时间以及最后已知正常 Commit/Tag。
3. 只在需要细节时读取通用规范对应章节。
4. 优先完成用户指定关键路径，不主动扩大范围。

### 5.2 未经授权不得执行

- 引入新框架、构建系统、依赖管理器或代码生成器。
- 重排厂商目录或大规模迁移生成代码。
- 升级 SDK、编译器或核心依赖。
- 全局格式化、重命名、清理和无关重构。
- 从零搭建复杂 CI、HIL、安全或发布系统。
- 删除尚未理解、可能影响硬件的代码。

### 5.3 高风险修改

修改时钟、链接脚本、中断、启动流程、主状态机、调度、协议或大量生成代码前，Agent 必须确认已有 checkpoint，或使用短期分支保护当前状态；不得覆盖用户未提交修改。

无法接触真实硬件时，只能报告静态检查和构建结果，不能声称“硬件验证通过”。

### 5.4 完成汇报

Agent 必须说明：

- 修改的功能和文件范围。
- 实际执行的构建、测试和检查。
- 已验证和未验证的硬件行为。
- 当前已知正常的 Commit/Tag（如果存在）。
- 剩余风险、回退方式和是否阻塞提交。

---

## 6. 快速检查清单

### 启动

- [ ] 首次构建、烧录和启动验证通过。
- [ ] 配置源、工程文件和工具版本已记录。
- [ ] README、DEVLOG、`.gitignore` 和远端备份已建立。
- [ ] 已创建 `checkpoint/baseline` 并保存基线固件。

### 每个里程碑

- [ ] 指定配置能够构建。
- [ ] 在目标硬件完成相关冒烟测试。
- [ ] 参数、已知问题和结果已写入 DEVLOG。
- [ ] Commit 清楚；真正可运行时才创建 checkpoint。

### 功能冻结

- [ ] 基本路径已经闭环，Must 功能完成。
- [ ] 已知正常 checkpoint 可以立即恢复。
- [ ] 停止新增功能、升级工具和大规模重构。
- [ ] 剩余工作只包含阻塞修复、测试和交付。

### 最终提交

- [ ] 使用最终硬件和固件完成至少两次完整演练。
- [ ] 最终 Tag 与固件来自同一 Commit。
- [ ] 源码、固件、README、版本信息和校验和齐全。
- [ ] 交付包位于至少两个独立位置。
- [ ] 已检查秘密、个人路径和资料许可。

### 冲刺结束后

- [ ] 将长期信息整理进 README、docs 或 Issue。
- [ ] 清理实验分支、临时代码和调试开关。
- [ ] 补充 Changelog、正式版本和 Release（需要维护时）。
- [ ] 将重复人工测试转成脚本、主机测试或 HIL。
- [ ] 回顾最大时间损失点，改进下一次项目模板。

---

# 附录 A：最小 README 模板

````markdown
# <Project Name>

<比赛/演示目标和当前状态。>

## Target and toolchain

- MCU/board/revision: <target>
- Programmer/debugger: <tool>
- Compiler/IDE/framework: <version>
- SDK/generator: <version>

## Build

```text
<exact command or exact GUI configuration>
```

Output: `<firmware path>`

## Flash and verify

```text
<flash command/address or exact operation>
```

Expected result: <LED/log/display/behavior>.

## Hardware and protocol

- Pinout: <path or short table>
- Serial: <port/baud/frame>
- Bus addresses: <list>
- Power/wiring constraints: <notes>

## Status

- Working: <list>
- Not working: <list>
- Last known good: `<commit or tag>`
- Known issues/workarounds: <list>
````

---

# 附录 B：最小 DEVLOG 模板

```markdown
# Sprint Devlog

## Current status

- Remaining time:
- Current priority:
- Last known good tag:
- Final target:

## Tasks

### Must
- [ ]

### Should
- [ ]

### Optional
- [ ]

## Known issues

| Issue | Probability/impact | Workaround | Blocking |
| --- | --- | --- | :---: |
| | | | No |

## Parameters and decisions

| Item | Value/decision | Test condition | Reason |
| --- | --- | --- | --- |
| | | | |

## Test log

| Time | Commit/tag | Hardware | Test | Result | Notes |
| --- | --- | --- | --- | --- | --- |
| | | | | | |
```

---

# 附录 C：通用规范索引

遇到未展开的问题时，查阅 [Development_Workflow.md](Development_Workflow.md)：

| 问题 | 章节 |
| --- | --- |
| 目录、根文件、生成代码、第三方依赖 | §1～§6 |
| Commit、分支、高级 Git、恢复 | §7～§12 |
| Issues、PR、Rulesets、Release、Actions | §13～§19 |
| 正式开发、Bug、实验、发布、回滚 | §20～§24 |
| 完整检查清单 | §25～§30 |

发生冲突时，安全、秘密、许可证和已共享 Git 历史规则不得降级；其余流程以本文的时间约束和裁剪表为准。
