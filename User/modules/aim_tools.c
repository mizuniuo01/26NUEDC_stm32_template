/**
 * @file    aim_tools.c
 * @brief   MaixCAM 目标切换通信临时测试模块。
 * @author  mizuniuo01
 * @date    2026-07-21
 * @version 1.0.0
 * @note    请求阶段每 20ms 发送 FF '1' FE，直到收到视觉端切换成功 ACK。
 * @note    ACK 后每 20ms 发送 FF '0' FE，等待 3 秒后重新请求切换。
 * @warning 本模块只用于当前通信联调，正式业务流程完成后应删除或替换。
 */

#include "aim_tools.h"
#include "cam.h"
#include <stm32f4xx_hal.h>

/** @brief 目标切换命令发送周期，单位为毫秒。 */
#define AIM_TOOLS_SEND_PERIOD_MS 20U

/** @brief 切换成功后的等待时间，单位为毫秒。 */
#define AIM_TOOLS_COOLDOWN_MS 3000U

/** @brief MaixCAM 请求切换目标的 ASCII payload。 */
#define AIM_TOOLS_SWITCH_REQUEST_BYTE ((uint8_t)'1')

/** @brief MaixCAM 取消切换请求的 ASCII payload。 */
#define AIM_TOOLS_SWITCH_IDLE_BYTE ((uint8_t)'0')

/** @brief 临时目标切换测试状态。 */
typedef enum {
    AIM_TOOLS_STATE_REQUEST_SWITCH = 0, /* 持续请求切换目标。 */
    AIM_TOOLS_STATE_COOLDOWN,           /* 发送 0 并等待下一轮测试。 */
} aim_tools_state_t;

/** @brief 当前目标切换测试状态。 */
static aim_tools_state_t aim_tools_state;

/** @brief 最近一次成功加入 cam 发送队列的时间。 */
static uint32_t aim_tools_last_send_tick_ms;

/** @brief 最近一次收到切换成功 ACK 的时间。 */
static uint32_t aim_tools_cooldown_start_tick_ms;

/**
 * @brief  在发送周期到达且 cam 队列为空时发送当前状态命令。
 * @param  command   ASCII '0' 或 '1'。
 * @param  tick_ms   当前 HAL tick。
 * @return 无。
 */
static void aim_tools_send_command(uint8_t command, uint32_t tick_ms)
{
    if ((uint32_t)(tick_ms - aim_tools_last_send_tick_ms) < AIM_TOOLS_SEND_PERIOD_MS) {
        return;
    }
    if (cam_get_pending_tx_count() != 0U) {
        return;
    }
    if (cam_send_packet(&command, 1U) == CAM_STATUS_OK) {
        aim_tools_last_send_tick_ms = tick_ms;
    }
}

/**
 * @brief  初始化临时目标切换通信测试状态机。
 * @return 无。
 */
void aim_tools_init(void)
{
    uint32_t tick_ms;

    tick_ms = HAL_GetTick();
    aim_tools_state = AIM_TOOLS_STATE_REQUEST_SWITCH;
    aim_tools_last_send_tick_ms = tick_ms - AIM_TOOLS_SEND_PERIOD_MS;
    aim_tools_cooldown_start_tick_ms = tick_ms;
    (void)cam_take_switch_ack();
}

/**
 * @brief  推进一次目标切换通信测试状态机。
 * @return 无。
 */
void aim_tools_task(void)
{
    uint32_t tick_ms;
    uint8_t command;

    tick_ms = HAL_GetTick();

    switch (aim_tools_state) {
        case AIM_TOOLS_STATE_REQUEST_SWITCH:
            if (cam_take_switch_ack()) {
                aim_tools_state = AIM_TOOLS_STATE_COOLDOWN;
                aim_tools_cooldown_start_tick_ms = tick_ms;
                aim_tools_last_send_tick_ms = tick_ms - AIM_TOOLS_SEND_PERIOD_MS;
            }
            break;

        case AIM_TOOLS_STATE_COOLDOWN:
            (void)cam_take_switch_ack();
            if ((uint32_t)(tick_ms - aim_tools_cooldown_start_tick_ms)
                >= AIM_TOOLS_COOLDOWN_MS) {
                aim_tools_state = AIM_TOOLS_STATE_REQUEST_SWITCH;
                aim_tools_last_send_tick_ms = tick_ms - AIM_TOOLS_SEND_PERIOD_MS;
            }
            break;

        default:
            aim_tools_state = AIM_TOOLS_STATE_REQUEST_SWITCH;
            aim_tools_last_send_tick_ms = tick_ms - AIM_TOOLS_SEND_PERIOD_MS;
            (void)cam_take_switch_ack();
            break;
    }

    command = (aim_tools_state == AIM_TOOLS_STATE_REQUEST_SWITCH)
                  ? AIM_TOOLS_SWITCH_REQUEST_BYTE
                  : AIM_TOOLS_SWITCH_IDLE_BYTE;
    aim_tools_send_command(command, tick_ms);
}
