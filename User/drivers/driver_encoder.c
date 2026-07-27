/**
 * @file driver_encoder.c
 * @brief 基于定时器正交编码模式采集车轮增量计数。
 */
#include "driver_encoder.h"

/**
 * @brief  初始化定时器正交编码器驱动并启动计数
 * @param  encoder 编码器驱动实例
 * @param  config 定时器、方向和计数器位宽配置
 * @retval STATUS_OK 编码器计数已启动
 * @retval STATUS_INVALID_ARGUMENT 配置为空、句柄为空或数值约束非法
 * @retval STATUS_IO_ERROR HAL 无法启动编码器定时器
 */
status_code_t driver_encoder_init(driver_encoder_t *encoder, const driver_encoder_config_t *config)
{
    if (!encoder || !config || !config->timer || (config->sign == 0) ||
        (config->counter_bits == 0U) || (config->counter_bits > 32U)) {
        return STATUS_INVALID_ARGUMENT;
    }
    encoder->config = *config;
    encoder->last_count = 0U;
    encoder->delta = 0;
    encoder->is_initialized = true;
    __HAL_TIM_SET_COUNTER(config->timer, 0U);
    return HAL_TIM_Encoder_Start(config->timer, TIM_CHANNEL_ALL) == HAL_OK ? STATUS_OK
                                                                           : STATUS_IO_ERROR;
}

/**
 * @brief  采样一次计数器并计算考虑回绕和安装方向的增量
 * @param  encoder 编码器驱动实例
 * @retval STATUS_OK 增量已经更新
 * @retval STATUS_NOT_INITIALIZED 驱动实例为空或尚未初始化
 */
status_code_t driver_encoder_process(driver_encoder_t *encoder)
{
    uint32_t count;
    int32_t difference;
    uint32_t mask;

    if (!encoder || !encoder->is_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    count = __HAL_TIM_GET_COUNTER(encoder->config.timer);
    mask = encoder->config.counter_bits >= 32U
               ? UINT32_MAX
               : ((UINT32_C(1) << encoder->config.counter_bits) - UINT32_C(1));
    difference = (int32_t)((count - encoder->last_count) & mask);
    if (encoder->config.counter_bits < 32U) {
        uint32_t half = UINT32_C(1) << (encoder->config.counter_bits - 1U);
        if ((uint32_t)difference >= half) {
            difference -= (int32_t)(UINT32_C(1) << encoder->config.counter_bits);
        }
    }
    encoder->last_count = count;
    encoder->delta = difference * encoder->config.sign;
    return STATUS_OK;
}

/**
 * @brief  读取最近一个采样周期的编码器增量
 * @param  encoder 编码器驱动实例
 * @return 最近一次有符号计数增量，encoder 为空时返回零
 */
int32_t driver_encoder_delta(const driver_encoder_t *encoder)
{
    return encoder ? encoder->delta : 0;
}
