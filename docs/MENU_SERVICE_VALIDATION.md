# OLED 菜单验证记录

验证日期：2026-07-27

Live 动作扩展验证日期：2026-07-29

基础菜单验证结果：全部通过

Live 动作扩展结果：全部通过

本记录对应正式启用的 Parameter Service、Menu Service、Menu Action Service、Application 适配和
OLED 异步刷新路径。本轮主机测试、LED 回调和注册表已在完整验证后删除，不进入正式工程。

## 按键映射

PCB 物理 K1–K4 与 `.ioc` 的 key1–key4 标签反序，BSP 按
`key4、key3、key2、key1、key5` 注册；上层始终使用 PCB 物理编号。

| PCB 物理按键 | `.ioc` 标签 | MCU 引脚 | 菜单基础功能 |
| --- | --- | --- | --- |
| K1 | key4 | PD13 | 上翻、增加、步长乘 10 |
| K2 | key3 | PD12 | 下翻、减少、步长除以 10 |
| K3 | key2 | PD11 | Debug 进入/确认/长按步长；Live 主动作 |
| K4 | key1 | PD10 | Debug 返回/取消；Live 次动作 |
| K5 | key5 | PC8 | Debug/Live 切换 |

## 已通过范围

- 默认 Debug 启动、分组/参数/数值/步长三级页面和全部提示栏。
- 五键基础映射、循环导航、组合键抑制、K5 优先级、800 ms 长按和 500/100 ms 连发。
- 共同速度 PID 默认值、范围钳制、草稿确认/取消、步长确认/取消和按参数记忆。
- Debug/Live 往返时页面位置、实时列表位置、数值草稿和步长编辑状态保持。
- Roll、Pitch、Yaw、左右编码器、超声波及相机 Target/X/Y 实时显示和无效值占位。
- Live 循环滚动、100 ms 目标刷新、Debug 250 ms 同步和交互后的即时刷新请求。
- OLED 忙时显存保护以及连续切换、滚动和编辑压力下的无花屏、无冻结、无阻塞表现。
- 参数提交同步左右速度环并清除旧 PID 动态历史，菜单本身不启动任何执行器。

## Live 动作扩展

- Menu Service 只发布 Primary/Secondary 逻辑动作，不直接依赖项目执行器。
- Menu Action Service 使用初始化期静态绑定表调用 Application 提供的非阻塞回调。
- 主机测试通过短按精确一次分派、长按和组合键抑制、离开 Live 清除事件、固定执行顺序、
  空绑定及回调错误传播。
- 上板测试通过 K3/Primary 翻转 LED1 和 K4/Secondary 翻转 LED2，确认实际按键、菜单事件、
  扩展服务和 Application 回调链路完整可用。
- 临时 LED 回调、状态和注册表已删除；正式 Application 保持空绑定，等待项目功能注册。

## 当前设计边界

- 参数只在 RAM 中生效，断电后恢复编译期默认值。
- 本轮没有 Flash 参数持久化和蓝牙调参协议。
- 正式模板尚未注册 Primary/Secondary 的项目功能，未绑定动作会被安全消费。
- 巡线传感器正式初始化保持关闭，不参与菜单启动路径。
