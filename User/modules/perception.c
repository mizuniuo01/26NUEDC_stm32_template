/**
 * @file    perception.c
 * @brief   环境感知模块（当前仅保留灰度 Pattern 数据封装）
 * @author  mizuniuo01
 * @date    2026-07-15
 */

#include "perception.h"
#include "sensor.h"
#include "pattern.h"

static perception_data_t data;

void perception_init(void)
{
    data.direction = 0;
    data.green = 0;
    data.stop_flag = 0;
    data.all_black_flag = 0;
    data.obstacle_flag = 0;
}

void perception_task(void)
{
    uint8_t sensor_raw;

    /* 每 tick 清除上一周期的标志位，由当前帧重新判定 */
    data.all_black_flag = 0;
    data.obstacle_flag = 0;

    /* 灰度 */
    sensor_raw = sensor_read_data();
    if (pattern_lookup(sensor_raw) == PATTERN_CROSS) {
        data.all_black_flag = 1;
    }
}

perception_data_t *perception_get_data(void)
{
    return &data;
}
