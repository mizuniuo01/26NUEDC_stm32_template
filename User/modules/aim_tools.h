#ifndef AIM_TOOLS_H
#define AIM_TOOLS_H

#include <stdint.h>

/* 瞄准工具接口状态。 */
typedef enum {
    AIM_TOOLS_STATUS_OK = 0,
    AIM_TOOLS_STATUS_INVALID_ARGUMENT = -1,
    AIM_TOOLS_STATUS_NOT_INITIALIZED = -2,
    AIM_TOOLS_STATUS_BUSY = -3,
    AIM_TOOLS_STATUS_QUEUE_FULL = -4,
    AIM_TOOLS_STATUS_IO_ERROR = -5,
} aim_tools_status_t;

/* 生命周期与非阻塞执行器任务。 */
aim_tools_status_t aim_tools_init(void);
void aim_tools_task(void);

/* 云台锁定、归位和软件命令角读取。 */
aim_tools_status_t aim_tools_track_target(int16_t error_x, int16_t error_y);
aim_tools_status_t aim_tools_return_home(void);
void aim_tools_get_gimbal_angles(float *x_deg, float *y_deg);
uint8_t aim_tools_target_is_centered(int16_t error_x, int16_t error_y);

/* 非阻塞射击控制。 */
aim_tools_status_t aim_tools_shoot(void);
uint8_t aim_tools_is_shooting(void);

#endif /* AIM_TOOLS_H */
