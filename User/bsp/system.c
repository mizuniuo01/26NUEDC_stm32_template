/**
 * @file    system.c
 * @brief   硬件资源注册中心 — 句柄定义 + 驱动初始化
 * @author  mizuniuo01
 * @date    2026-06-01
 * @version 1.0.0
 * @note    本项目所有硬件句柄的唯一定义处，外部只能通过 system.h 的 getter 访问。
 * @note    新增模块：定义 static 句柄 → 添加 getter → system_init 中调用 xxx_init。
 * @note    平台初始化（SYSCFG_DL_init/时钟/NVIC/__enable_irq）在 main.c 中，
 *          system_init 只做驱动层 init，由 main.c 在平台就绪后调用。
 * @warning getter 返回的是句柄指针，调用方不可 free 或修改句柄内部字段。
 *
 * @usage
 * 项目级模板文件。复制到目标工程后，按实际使用的模块增删：
 *
 * 1. 句柄实体：保留用到的，删除或注释不用的
 * 2. getter 函数：与句柄实体一一对应
 * 3. system_init()：只写驱动 xxx_init()，平台初始化（SYSCFG_DL_init 等）在 main.c
 *
 * 外部文件通过 system.h 的 getter 拿句柄：
 *
 * led_toggle(system_led1());
 * motor_set_speed(system_motor_left(), 500);
 */

#include "system.h"
#include "gpio.h"
#include "usart.h"
#include "i2c.h"
#include "tim.h"
#include "iwdg.h"
#include "blueteeth.h"
#include "gyroscope.h"
#include "sensor.h"
#include "pwm.h"
#include "cam.h"
#include "motor.h"
#include "encoder.h"
#include "motion_control.h"
#include "motion_manager.h"
#include "perception.h"

/* 系统运行标志位 */
volatile static uint8_t system_led_flag = 0;

/* 句柄实体（全部 static，外部不可直接访问） */
static led_handle_t led1;
static led_handle_t led2;
static led_handle_t led3;
static led_handle_t led4;
static buzzer_handle_t buzzer;
static motor_handle_t motor;

/* getter：只返回指针，不暴露实体 */

led_handle_t *system_led1(void)
{
    return &led1;
}

led_handle_t *system_led2(void)
{
    return &led2;
}

led_handle_t *system_led3(void)
{
    return &led3;
}

led_handle_t *system_led4(void)
{
    return &led4;
}

buzzer_handle_t *system_buzzer(void)
{
    return &buzzer;
}

motor_handle_t *system_motor(void)
{
    return &motor;
}

pid_t *system_pid_speed_left(void)
{
    return motion_control_pid_speed_left();
}

pid_t *system_pid_speed_right(void)
{
    return motion_control_pid_speed_right();
}

pid_t *system_pid_angle(void)
{
    return motion_control_pid_angle();
}

void set_system_led_flag(uint8_t state)
{
    system_led_flag = state;
}

/**
 * @brief  系统状态处理函数（在 main.c 的 while(1) 中调用）
 * @param  无
 * @retval 无
 */
void system_state(void)
{
    static uint8_t startup_done = 0;
    static uint8_t wdog_blink_left = 0; /* 快闪剩余次数（每 100ms 一次） */
    static uint8_t heartbeat_led_cnt = 0; /* 心跳 LED 翻转计数器 */
    static uint8_t heartbeat_led_flag = 0; /* 心跳 LED 翻转标志位 */
    static uint8_t watchdog_led_flag = 0; /* 看门狗快闪标志位 */

    HAL_IWDG_Refresh(&hiwdg);

    /* 上电一次性检测：是否被 IWDG 复位 */
    if (!startup_done) {
        startup_done = 1;
        if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) {
            __HAL_RCC_CLEAR_RESET_FLAGS();
            wdog_blink_left = 30; /* 30 次 × 50ms = 1.5 秒快闪 */
        }
    }

    /* 50ms 节拍：心跳倍频 + 看门狗快闪 */
    if (system_led_flag) {
        system_led_flag = 0;

        heartbeat_led_cnt++;
        if (heartbeat_led_cnt >= 20) {
            heartbeat_led_cnt = 0;
            heartbeat_led_flag = 1;
        }
        watchdog_led_flag = 1;
    }

    /* 系统心跳：LED4 每秒翻转一次 */
    if (heartbeat_led_flag) {
        heartbeat_led_flag = 0;
        led_toggle(&led4);
    }

    /* 被狗咬快闪：LED3 每 50ms 翻转，持续 1.5 秒后自动停止 */
    if (wdog_blink_left > 0) {
        if (watchdog_led_flag) {
            watchdog_led_flag = 0;
            wdog_blink_left--;
            led_toggle(&led3);
        }
        /* 快闪结束后确保 LED3 熄灭 */
        if (wdog_blink_left == 0) {
            led_off(&led3);
        }
    }
}

/**
 * @brief  初始化所有驱动模块（在 main.c 进行平台初始化）
 * @param  无
 * @retval 无
 */
void system_init(void)
{
    /* GPIO 控制类 */
    led_cfg_t led1_cfg = {.port = GPIOB, .pin = led1_Pin, .active_level = 1};
    led_init(&led1, &led1_cfg);
    led_cfg_t led2_cfg = {.port = GPIOB, .pin = led2_Pin, .active_level = 1};
    led_init(&led2, &led2_cfg);
    led_cfg_t led3_cfg = {.port = GPIOB, .pin = led3_Pin, .active_level = 1};
    led_init(&led3, &led3_cfg);
    led_cfg_t led4_cfg = {.port = GPIOB, .pin = led4_Pin, .active_level = 1};
    led_init(&led4, &led4_cfg);

    buzzer_cfg_t buzzer_cfg = {.port = GPIOA, .pin = buzzer_Pin, .active_level = 1};
    buzzer_init(&buzzer, &buzzer_cfg);

    motor_cfg_t motor_cfg = {
        .l_port = GPIOA,
        .r_port = GPIOC,
        .l_nsleep_pin = sleepl_Pin,
        .r_nsleep_pin = sleepr_Pin,
        .l_ph_pin = dirl_Pin,
        .r_ph_pin = dirr_Pin,
    };
    motor_init(&motor, &motor_cfg);

    /* 通信单实例 */
    blueteeth_init(&huart1);
    gyro_init(&huart6);
    cam_init(&huart3);
    /* oled_init(I2C_OLED_INST); */

    /* 驱动 / 执行器类 */
    sensor_init(&hi2c2);
    pwm_init(&htim3);
    encoder_init(&htim2, &htim1);

    motion_control_init();
    motion_manager_init();
    perception_init();
}
