# AutoBallCar 底层重构架构说明

本工程是基于 `AutoBallCar.ioc` 的底层重构基线。默认构建交付已验证的 Driver、BSP、
Services、Domain 和 Libraries，并由 `User/app/app.c` 组合当前可运行功能；不携带临时上板
测试编排。

## 分层

```text
Core/          CubeMX 生成的启动、HAL、MSP 和中断入口
User/drivers/  STM32 外设和固定设备协议，只实现机制，不决定产品策略
User/bsp/      本板卡实例、引脚/外设绑定、生命周期、安全状态和 ISR 路由
User/services/ 无硬件业务的状态、参数、菜单、错误事件、命令帧解析、蓝牙协议和集中诊断显示
User/domain/   纯逻辑，例如八路传感器模式解释
User/app/      产品组合根、服务注册表、BSP 端口适配和主循环编排
```

驱动不公开 HAL 句柄给上层；BSP 只公开语义能力，例如 `bsp_drive_set`、`bsp_line_sensor_get` 和 `bsp_oled_refresh`。所有状态码由驱动产生，错误服务只记录事件，不直接刷新显示器。

## CubeMX 生成边界

`AutoBallCar.ioc` 是 MCU 引脚、时钟和外设配置的唯一权威来源。`Core/` 除 `Core/Src/main.c`
中的 CubeMX 用户区外，不接受手工业务改动；外设配置变化必须先修改 `.ioc`，再由 CubeMX 重新生成。

`main.c` 只承担组合根入口，项目代码必须限制在以下用户区：

- `USER CODE BEGIN Includes`：包含 Application 公共头文件。
- `USER CODE BEGIN 2`：启动定时器并调用 `app_init()`。
- `USER CODE BEGIN 3`：调用 `app_run_once()`。

HAL 回调实现在 `User/bsp/bsp_board_stm32_callbacks.c`，由该平台适配文件有界转发给 BSP，
不占用 `main.c` 的 `USER CODE BEGIN 4` 区域。

不得改写 `main()` 生成骨架、外设初始化列表、`SystemClock_Config()` 或用户区以外的生成代码。每次
CubeMX 重新生成后，必须检查上述入口仍然存在并完成一次构建验证。

## 本板绑定

| 能力 | `.ioc` 资源 | 约束 |
| --- | --- | --- |
| 左/右电机 | TIM3 CH1/CH2、PA4/PA5、PC4/PC5 | 84 MHz / 4200 = 20 kHz；ARR=4199；约 3% 死区；初始化关闭 nSLEEP 和 PWM |
| 左/右编码器 | TIM2、TIM1 | 保留左负号、右正号的机械方向约定 |
| MCU 线传感器 | I2C2 PB10/PB11 + DMA | 7 位地址 `0x4c`、命令 `0xdd`；返回 8 位黑线掩码 |
| OLED | I2C3 PA8/PC9 | SSD1306 128×64，非阻塞分页刷新，无菜单 |
| 超声波 | TIM4 CH4、PD14/PD15 | 输入捕获；测距流程非阻塞 |
| 按键 | PD10–PD13、PC8 | PCB K1–K4 与 `.ioc` 标签反序；BSP 统一为物理编号后提供 5 路快照和去抖事件 |
| 蓝牙/相机/陀螺仪 | USART1/3/6 | UART 流驱动收发；蓝牙小程序显示、绘图和发送排队由服务层负责，蓝牙命令帧交给命令服务；相机转义/CRC 和陀螺仪 0x55/0x53 校验留在协议驱动 |
| 舵机 | UART4 | 注册式 ID 表，注册几个即管理几个；初始化不移动 |
| 步进电机 | USART2 | ZDT X42S `0xFD` 位置协议，DMA 发送，初始化不使能 |

PA15、PC12、PD0–PD4 的七个只读 GPIO 传感器本轮不接入；激光器因没有独立 `.ioc` 引脚也不接入，PD11 仍保留为 `key2`。

## 生命周期与并发

`main` 完成 CubeMX 初始化后调用 `app_init()`，主循环只调用 `app_run_once()`。Application
按 BSP、Domain 对象、Parameter Service、Menu Service 的顺序初始化，并在每轮先推进
`bsp_board_process()`，再推进上层服务。BSP 一次建立全部已配置能力，不再包含按测试阶段
提前返回的编译分支。TIM6、HAL UART/I2C/TIM 回调只做有界转发；轮询、DMA 发起和 OLED
分页发送均在主循环处理。

所有执行器默认安全关闭，只有显式调用 `bsp_drive_enable`、`bsp_stepper_enable` 等接口才允许输出。看门狗由 BSP 统一刷新，用于监督主循环是否持续推进；业务传感器离线只记录健康状态和诊断事件，不单独阻止喂狗。核心电机、编码器和 I2C 传感器初始化失败时启动失败，可选设备不可用只记录诊断事件。

蓝牙数据格式化和发送队列由 `User/services/bluetooth_service.c` 管理，手机端显示字段由 `User/services/display.c` 集中配置；两者保留为正式服务，由未来 Application 决定何时初始化和推进。驱动和 BSP 只维护最新快照，ISR 不执行格式化或蓝牙发送。

双编码器保持精确 10 ms 采样，速度控制器提供已上板整定的默认初始化接口：`Kp=15`、
`Ki=1`、`Kd=0`、积分限幅 `4000`、归一化输出限幅 `±1000`。BSP 将千分比命令线性换算为
TIM3 的 `±4200` 比较值，并使用 126 计数的约 3% 死区，因此 PWM 周期变化不会改变现有
PID 参数对应的物理占空比。BSP 启动时校验 TIM3 ARR 必须为 4199，防止 CubeMX 配置与软件
尺度不一致。控制器不在 BSP 中自动启动，后续上层通过快照序号每个样本只推进一次。TIM4
继续使用 1 MHz 计数器生成非阻塞 10 us 触发脉冲，并在 40 ms 无下降沿时恢复超声波捕获状态。

通信驱动必须与 CubeMX 的 DMA 配置保持一致：USART1/2/3/6 和 I2C2 已配置的方向均使用 DMA；只有未配置 DMA 的 UART4 和 I2C3 允许使用中断异步传输。

按照当前硬件使用状态，`BSP_LINE_SENSOR_ENABLED` 保持为 `0`：BSP 不初始化巡线传感器、不发起 I2C2 周期读取。驱动和 BSP 公共接口保留，恢复硬件后可重新启用。

舵机驱动当前注册 ID 12，初始化不产生任何动作；如需增减舵机，只修改 BSP 注册 ID 表。

步进电机发送接口保留原版的角度、速度、加速度、三种位置模式和同步标志语义。线上 16 字节帧为：地址、`0xFD`、方向、加速度、减速度、速度、角度放大 100 倍的大端数值、模式、同步标志和 `0x6B`。USART2 已由 CubeMX 配置 DMA 收发，命令发送使用 TX DMA。

## 参数服务与 OLED 菜单

`User/services/parameter_service.c` 是独立的强类型参数入口。它只保存静态元数据和回调绑定，
不拥有 PID 等实际参数。每个描述符显式给出稳定 ID、`float`/`int32_t`/`uint32_t`/`bool`
类型、闭区间、默认步长、小数位、权限和读写回调；注册数量由静态数组直接推导。初始化会
检查类型、范围、步长、重复 ID、容量和所有者当前值，后续 OLED、蓝牙或其他入口均应通过
同一个服务读写参数。菜单渲染使用注册表索引遍历；通信等非界面入口使用
`parameter_service_get_by_id()`、`parameter_service_set_by_id()` 和
`parameter_service_adjust_by_id()` 按全局稳定 ID 访问，不依赖菜单排列顺序。

`User/services/menu_service.c` 是无板卡绑定的服务。Application 注入单调时间、五键状态和
OLED 像素端口，当前 AutoBallCar 适配全部集中在 `User/app/app.c`。OLED 显存由菜单独占，
上一帧异步发送期间 `bsp_oled_frame_ready()` 返回未就绪，菜单不会清屏或修改显存。

菜单默认进入 Debug 模式，K5 在 Debug/Live 间切换。Debug 保留“参数组 → 参数 → 数值/步长”
三级操作；数值先写草稿，确认时重读权威值，检测到其他入口修改后拒绝覆盖并重新载入。
K1/K2 调节并在 500 ms 后以 100 ms 周期连发，K3 短按进入或确认、长按 800 ms 进入步长
编辑，K4 返回。步长按参数保留，数值钳制到注册范围，列表首尾循环。

当前仅注册共同速度 PID：Kp、Ki、Kd、输出限幅和积分限幅。提交任一字段时通过
`speed_controller_apply_common_speed_pid()` 原子同步左右速度环并清除 PID 动态历史。参数
只在 RAM 中生效，本轮没有 Flash 持久化或蓝牙调参协议。

Live 模式以 100 ms 目标周期显示陀螺仪 Roll/Pitch/Yaw、左右编码器 10 ms 周期增量、超声波
距离以及相机目标/X/Y 快照。K1/K2 滚动，一屏显示不下时循环浏览；无有效快照显示 `--`。
Debug 的周期刷新为 250 ms，交互事件会立即请求下一帧。

## 验证

本轮硬件范围内的蓝牙、I2C 巡线传感器、陀螺仪、底盘电机与编码器、速度环、OLED、按键、
LED、蜂鸣器、超声波、MaixCAM 和 ID 12 舵机均已完成此前底层上板验证。步进电机按已确认的
USART2 DMA 配置和原版线上协议保留，未安排机械动作验证。角度环和运动规划等待装车后验证。

Parameter Service 和 Menu Service 已完成开发期主机回归及完整上板验证，覆盖强类型范围、
速度 PID 同步、三级导航、草稿冲突、步长记忆、长按连发、模式切换、实时数据显示、列表
滚动和 OLED 异步刷新稳定性。正式固件默认启用菜单，验证结果与 PCB 按键映射见
`docs/MENU_SERVICE_VALIDATION.md`。
