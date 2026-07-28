#ifndef AUTO_BALL_CAR_USER_BSP_BSP_BOARD_CONFIG_H
#define AUTO_BALL_CAR_USER_BSP_BSP_BOARD_CONFIG_H /* 头文件保护 */

#define BSP_SENSOR_I2C_ADDRESS_7BIT 0x4CU /* 巡线传感器的 7 位 I2C 地址 */
#define BSP_SENSOR_READ_COMMAND 0xDDU /* 巡线传感器读取命令 */
#define BSP_LINE_SENSOR_ENABLED 0U /* 暂停巡线传感器初始化和周期访问 */
#define BSP_OLED_I2C_ADDRESS_HAL 0x78U /* HAL 接口使用的 OLED 左移一位地址 */
#define BSP_MOTOR_COMMAND_LIMIT 1000 /* 上层电机命令对称限幅，单位：千分比 */
#define BSP_MOTOR_PWM_PRESCALER 0U /* TIM3 预分频寄存器值，零表示不分频 */
#define BSP_MOTOR_PWM_PERIOD_TICKS 4200U /* 20 kHz 电机 PWM 计数周期，单位：计数 */
#define BSP_MOTOR_PWM_DEAD_ZONE_TICKS 126U /* 约 3% 电机最小有效比较值，单位：计数 */
#define BSP_SENSOR_PERIOD_MS 3U /* 巡线传感器请求周期，单位：毫秒 */
#define BSP_SENSOR_STALE_MS 100U /* 巡线传感器数据失效时间，单位：毫秒 */
#define BSP_BOARD_TIMER_PERIOD_MS 1U /* TIM6 板级时间基准周期，单位：毫秒 */
#define BSP_ENCODER_PERIOD_MS 10U /* 编码器采样周期，单位：毫秒 */
#define BSP_KEY_DEBOUNCE_MS 20U /* 按键消抖时间，单位：毫秒 */
#define BSP_ULTRASONIC_PERIOD_MS 60U /* 超声波触发周期，单位：毫秒 */
#define BSP_SERVO_ID_1 12U /* 当前注册的舵机 ID */
#define BSP_SERVO_INTERVAL_MS 1000U /* 舵机默认动作时间，单位：毫秒 */
#define BSP_SERVO_POWER 0U /* 舵机命令使用的默认功率参数 */
#define BSP_STEPPER_ID_LEFT 1U /* 左侧步进电机总线 ID */
#define BSP_STEPPER_ID_RIGHT 2U /* 右侧步进电机总线 ID */

#endif /* AUTO_BALL_CAR_USER_BSP_BSP_BOARD_CONFIG_H */
