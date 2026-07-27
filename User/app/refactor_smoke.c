/**
 * @file refactor_smoke.c
 * @brief 被动验证重构后底层接口可调用性的冒烟应用。
 */
#include "refactor_smoke.h"
#include "bsp_board.h"
#include "error_service.h"
#include "line_pattern.h"

static line_pattern_result_t last_pattern;

/** @brief 初始化冒烟应用并确保电机保持禁用。 */
void refactor_smoke_init(void)
{
    last_pattern = line_pattern_decode(0U, false);
    (void)bsp_drive_disable();
}

/** @brief 处理一次传感器快照，同时保持所有执行机构禁用。 */
void refactor_smoke_process(void)
{
    bsp_sensor_snapshot_t sensor;

    if (bsp_line_sensor_get(&sensor) == STATUS_OK) {
        last_pattern = line_pattern_decode(sensor.value, sensor.is_valid);
    }
    /* 冒烟应用只验证接口连通性，因此不会使能任何执行机构。 */
    (void)last_pattern;
    (void)error_service_sequence();
}
