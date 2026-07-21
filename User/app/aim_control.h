#ifndef AIM_CONTROL_H
#define AIM_CONTROL_H

#include <stdint.h>

/* 瞄准调度层状态。 */
typedef enum {
    AIM_CONTROL_STATUS_OK = 0,
    AIM_CONTROL_STATUS_NOT_INITIALIZED = -1,
    AIM_CONTROL_STATUS_IO_ERROR = -2,
} aim_control_status_t;

/* 生命周期与自动目标锁定调度。 */
aim_control_status_t aim_control_init(void);
void aim_control_task(void);

#endif /* AIM_CONTROL_H */
