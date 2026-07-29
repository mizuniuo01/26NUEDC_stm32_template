# 步进电机硬件测试

本目录只放测试阶段使用的代码。当前 CMake 默认开启
`ENABLE_STEPPER_HARDWARE_TEST`，正式构建时使用
`-DENABLE_STEPPER_HARDWARE_TEST=OFF` 排除该测试。

## 接线与前提

- X42S 地址为 1，UART 为 115200 8-N-1，固定校验字节为 `0x6B`。
- `S_PosTDP` 已开启，位置按 0.01°/LSB 编码。
- 电机只需连接 USART2 TX、USART2 RX 和公共 GND；`En` 配置为 `Hold`，不接 EN 线。
- 蓝牙监视使用项目现有 USART1 链路。

## 蓝牙命令

- `@stepper_run#`：从头运行一次完整测试。
- `@stepper_status#`：打印当前状态、最近控制应答和最近实时位置。
- `@stepper_stop#`：立即中止测试，随后发送停止和失能命令。

上电只打印 `READY`，不会自动使能或运动。完整测试依次检查参数防御、UART
使能、位置清零、零角度、三种位置模式、正反方向、实时位置回读、运动中立即
停止、停止后回零、失能和失能后拒绝运动。精确位置检查允许误差为 ±1.0°；
运动中停止测试会发出 180° 命令，100 ms 后停止，并确认最终位置未到达 170°。
全部检查通过时结束日志应为 `[stepper-test] DONE pass=56 fail=0`。

测试尚未在实机执行。烧录后请保存从 `START` 到 `DONE` 或 `ABORTED` 的完整
蓝牙日志，用于定位首个失败步骤。
