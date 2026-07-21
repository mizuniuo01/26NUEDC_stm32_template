#ifndef PERCEPTION_H
#define PERCEPTION_H

#include <stdint.h>

/* 感知模块接口状态。 */
typedef enum {
    PERCEPTION_STATUS_OK = 0,
    PERCEPTION_STATUS_INVALID_ARGUMENT = -1,
    PERCEPTION_STATUS_NOT_READY = -2,
    PERCEPTION_STATUS_BUSY = -3,
    PERCEPTION_STATUS_NOT_CONFIGURED = -4,
    PERCEPTION_STATUS_NOT_MEASURABLE = -5,
    PERCEPTION_STATUS_IO_ERROR = -6,
} perception_status_t;

/* 上层消费的摄像头目标数据。 */
typedef struct {
    int16_t error_x;
    int16_t error_y;
    uint32_t update_tick_ms;
    uint8_t has_target;
} perception_target_data_t;

/* 生命周期与非阻塞感知任务。 */
void perception_init(void);
void perception_task(void);

/* 每份目标报告只被上层消费一次。 */
perception_status_t perception_take_target_data(perception_target_data_t *data);

/* 摄像头目标切换事务。 */
perception_status_t perception_request_target_switch(void);
uint8_t perception_is_target_switch_pending(void);
uint8_t perception_take_target_switch_ack(void);

/* 车辆偏航角及跨 0/360 度边界的最短角差。 */
perception_status_t perception_get_vehicle_yaw_deg(float *yaw_deg);
float perception_shortest_yaw_delta_deg(float reference_deg, float current_deg);

/* 根据当前 Y 舵机角度计算从镜头光心到地面目标点的水平距离。 */
perception_status_t perception_calculate_horizontal_distance(float y_axis_angle_deg,
    float *distance_mm);

#endif /* PERCEPTION_H */
