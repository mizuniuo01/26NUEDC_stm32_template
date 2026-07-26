/**
 * @file refactor_smoke.c
 * @brief Passive smoke application for the refactored bottom layers.
 */
#include "refactor_smoke.h"

#include "bsp_board.h"
#include "error_service.h"
#include "line_pattern.h"

static line_pattern_result_t last_pattern;

void refactor_smoke_init(void)
{
    last_pattern = line_pattern_decode(0U, 0U);
    (void)bsp_drive_disable();
}

void refactor_smoke_process(void)
{
    bsp_sensor_snapshot_t sensor;

    if (bsp_line_sensor_get(&sensor) == STATUS_OK) {
        last_pattern = line_pattern_decode(sensor.value, sensor.valid);
    }
    /* The smoke target deliberately keeps all actuators disabled. */
    (void)last_pattern;
    (void)error_service_sequence();
}
