/**
 * @file    perception.c
 * @brief   环境感知模块（视觉 + 灰度 + 超声波数据封装）
 * @author  mizuniuo01
 * @date    2026-07-15
 */

#include "perception.h"
#include "cam.h"
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
    cam_data_t cam;
    uint8_t sensor_raw;

    /* 每 tick 清除上一周期的标志位，由当前帧重新判定 */
    data.all_black_flag = 0;
    data.obstacle_flag = 0;

    /* 视觉 */
    cam = cam_get_data();
    if (cam.direction != DIRECTION_INVALID) {
        data.direction = cam.direction;
    }
    data.green = cam.green;
    data.stop_flag = cam.stop;

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
