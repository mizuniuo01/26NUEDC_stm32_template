# AutoBallCar 底层重构架构说明

本工程是基于 `AutoBallCar.ioc` 的底层重构基线。默认构建交付已验证的 Driver、BSP、Services、Domain 和 Libraries，不携带上板测试编排或产品业务流程。

## 分层

```text
Core/          CubeMX 生成的启动、HAL、MSP 和中断入口
User/drivers/  STM32 外设和固定设备协议，只实现机制，不决定产品策略
User/bsp/      本板卡实例、引脚/外设绑定、生命周期、安全状态和 ISR 路由
User/services/ 无硬件业务的状态、错误事件、命令帧解析、蓝牙协议和集中诊断显示
User/domain/   纯逻辑，例如八路传感器模式解释
User/app/      为后续产品组合根和业务编排预留
```

驱动不公开 HAL 句柄给上层；BSP 只公开语义能力，例如 `bsp_drive_set`、`bsp_line_sensor_get` 和 `bsp_oled_refresh`。所有状态码由驱动产生，错误服务只记录事件，不直接刷新显示器。

## CubeMX 生成边界

`AutoBallCar.ioc` 是 MCU 引脚、时钟和外设配置的唯一权威来源。`Core/` 除 `Core/Src/main.c`
中的 CubeMX 用户区外，不接受手工业务改动；外设配置变化必须先修改 `.ioc`，再由 CubeMX 重新生成。

`main.c` 只承担组合根入口，项目代码必须限制在以下用户区：

- `USER CODE BEGIN Includes`：包含 BSP 公共头文件。
- `USER CODE BEGIN 2`：启动定时器并初始化 BSP。
- `USER CODE BEGIN 3`：推进 BSP 主循环。

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
| 蓝牙/相机/陀螺仪 | USART1/3/6 | UART 流驱动收发；蓝牙小程序显示、绘图和发送排队由服务层负责，蓝牙命令帧交给命令服务；相机转义/CRC 和陀螺仪 0x55/0x53 校验留在协议驱动 |
| 舵机 | UART4 | 注册式 ID 表，注册几个即管理几个；初始化不移动 |
| 步进电机 | USART2 | ZDT X42S `0xFD` 位置协议，DMA 发送，初始化不使能 |

PA15、PC12、PD0–PD4 的七个只读 GPIO 传感器本轮不接入；激光器因没有独立 `.ioc` 引脚也不接入，PD11 仍保留为 `key2`。

## 生命周期与并发

`main` 完成 CubeMX 初始化后调用 `bsp_board_init`，主循环只调用 `bsp_board_process`。BSP 一次建立全部已配置能力，不再包含按测试阶段提前返回的编译分支。TIM6、HAL UART/I2C/TIM 回调只做有界转发；轮询、DMA 发起和 OLED 分页发送均在主循环处理。

所有执行器默认安全关闭，只有显式调用 `bsp_drive_enable`、`bsp_stepper_enable` 等接口才允许输出。看门狗由 BSP 统一刷新，用于监督主循环是否持续推进；业务传感器离线只记录健康状态和诊断事件，不单独阻止喂狗。核心电机、编码器和 I2C 传感器初始化失败时启动失败，可选设备不可用只记录诊断事件。

蓝牙数据格式化和发送队列由 `User/services/bluetooth_service.c` 管理，手机端显示字段由 `User/services/display.c` 集中配置；两者保留为正式服务，由未来 Application 决定何时初始化和推进。驱动和 BSP 只维护最新快照，ISR 不执行格式化或蓝牙发送。

双编码器保持精确 10 ms 采样，速度控制器提供已上板整定的默认初始化接口：`Kp=15`、`Ki=1`、`Kd=0`、积分限幅 `4000`、PWM 输出限幅 `±1000`。控制器不在 BSP 中自动启动，后续上层通过快照序号每个样本只推进一次。TIM4 继续使用 1 MHz 计数器生成非阻塞 10 us 触发脉冲，并在 40 ms 无下降沿时恢复超声波捕获状态。

通信驱动必须与 CubeMX 的 DMA 配置保持一致：USART1/2/3/6 和 I2C2 已配置的方向均使用 DMA；只有未配置 DMA 的 UART4 和 I2C3 允许使用中断异步传输。

按照当前硬件使用状态，`BSP_LINE_SENSOR_ENABLED` 保持为 `0`：BSP 不初始化巡线传感器、不发起 I2C2 周期读取。驱动和 BSP 公共接口保留，恢复硬件后可重新启用。

舵机驱动当前注册 ID 12，初始化不产生任何动作；如需增减舵机，只修改 BSP 注册 ID 表。

步进电机发送接口保留原版的角度、速度、加速度、三种位置模式和同步标志语义。线上 16 字节帧为：地址、`0xFD`、方向、加速度、减速度、速度、角度放大 100 倍的大端数值、模式、同步标志和 `0x6B`。USART2 已由 CubeMX 配置 DMA 收发，命令发送使用 TX DMA。

## 验证

本轮硬件范围内的蓝牙、I2C 巡线传感器、陀螺仪、底盘电机与编码器、速度环、OLED、按键、LED、蜂鸣器、超声波、MaixCAM 和 ID 12 舵机均已完成上板验证。步进电机按已确认的 USART2 DMA 配置和原版线上协议保留，未安排机械动作验证。角度环和运动规划等待装车后验证。
