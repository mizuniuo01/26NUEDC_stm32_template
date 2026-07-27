# AutoBallCar 底层重构架构说明

本工程是基于 `AutoBallCar.ioc` 的底层重构测试工程。旧的自动投球、瞄准、感知和运动产品流程不再属于默认构建目标；它们的硬件支持代码只在被新 BSP 或服务明确需要时迁移。

## 分层

```text
Core/          CubeMX 生成的启动、HAL、MSP 和中断入口
User/drivers/  STM32 外设和固定设备协议，只实现机制，不决定产品策略
User/bsp/      本板卡实例、引脚/外设绑定、生命周期、安全状态和 ISR 路由
User/services/ 无硬件业务的状态、错误事件、命令帧解析
User/domain/   纯逻辑，例如八路传感器模式解释
User/app/      极薄的 refactor_smoke，只验证能力，不驱动产品动作
```

驱动不公开 HAL 句柄给上层；BSP 只公开语义能力，例如 `bsp_drive_set`、`bsp_line_sensor_get` 和 `bsp_oled_refresh`。所有状态码由驱动产生，错误服务只记录事件，不直接刷新显示器。

## CubeMX 生成边界

`AutoBallCar.ioc` 是 MCU 引脚、时钟和外设配置的唯一权威来源。`Core/` 除 `Core/Src/main.c`
中的 CubeMX 用户区外，不接受手工业务改动；外设配置变化必须先修改 `.ioc`，再由 CubeMX 重新生成。

`main.c` 只承担组合根入口，项目代码必须限制在以下用户区：

- `USER CODE BEGIN Includes`：包含 BSP 和 Application 公共头文件。
- `USER CODE BEGIN 2`：启动定时器、初始化 BSP 和 Application。
- `USER CODE BEGIN 3`：推进 BSP 和 Application 主循环。

HAL 回调实现在 `User/bsp/bsp_board_stm32_callbacks.c`，由该平台适配文件有界转发给 BSP，
不占用 `main.c` 的 `USER CODE BEGIN 4` 区域。

不得改写 `main()` 生成骨架、外设初始化列表、`SystemClock_Config()` 或用户区以外的生成代码。每次
CubeMX 重新生成后，必须检查上述入口仍然存在并完成一次构建验证。

## 本板绑定

| 能力 | `.ioc` 资源 | 约束 |
| --- | --- | --- |
| 左/右电机 | TIM3 CH1/CH2、PA4/PA5、PC4/PC5 | PWM ARR=1000；约 3% 死区；初始化关闭 nSLEEP 和 PWM |
| 左/右编码器 | TIM2、TIM1 | 保留左负号、右正号的机械方向约定 |
| MCU 线传感器 | I2C2 PB10/PB11 + DMA | 7 位地址 `0x4c`、命令 `0xdd`；返回 8 位黑线掩码 |
| OLED | I2C3 PA8/PC9 | SSD1306 128×64，非阻塞分页刷新，无菜单 |
| 超声波 | TIM4 CH4、PD14/PD15 | 输入捕获；测距流程非阻塞 |
| 按键 | PD10–PD13、PC8 | 5 路快照和有限去抖事件，无菜单 |
| 蓝牙/相机/陀螺仪 | USART1/3/6 | UART 流驱动接收；蓝牙帧交给命令服务，相机转义/CRC 和陀螺仪 0x55/0x53 校验留在协议驱动 |
| 舵机 | UART4 | 固定协议驱动，初始化不移动 |
| 步进电机 | USART2 | ZDT X42S 协议驱动，初始化不使能 |

PA15、PC12、PD0–PD4 的七个只读 GPIO 传感器本轮不接入；激光器因没有独立 `.ioc` 引脚也不接入，PD11 仍保留为 `key2`。

## 生命周期与并发

`main` 完成 CubeMX 初始化后调用 `bsp_board_init`，主循环只调用 `bsp_board_process` 和 `refactor_smoke_process`。TIM6、HAL UART/I2C/TIM 回调只做有界转发；轮询、DMA 发起和 OLED 分页发送均在主循环处理。

所有执行器默认安全关闭，只有显式调用 `bsp_drive_enable`、`bsp_stepper_enable` 等接口才允许输出。看门狗由 BSP 健康监督统一刷新；核心电机、编码器和 I2C 传感器初始化失败时启动失败，可选设备不可用只记录诊断事件。

## 验证

在 Windows 的 CubeIDE/CMake 工具链中执行交叉编译；静态检查关注 C11、snake_case、状态码检查、ISR 有界性和公共接口不泄漏 HAL。上板后依次验证安全启动、编码器快照、I2C 传感器快照、OLED 刷新、按键事件、超声波读数和串口协议，产品动作保持关闭。
