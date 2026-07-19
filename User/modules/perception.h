#ifndef PERCEPTION_H
#define PERCEPTION_H

#include <stdint.h>

/* 感知参数 */
#define OBSTACLE_THRESH_MM 100.0f  /* 障碍物距离阈值（mm） */
typedef enum {
    DIRECTION_STRAIGHT = 0,
    DIRECTION_RIGHT    = 1,
    DIRECTION_LEFT     = 2,
    DIRECTION_INVALID  = 3,
} direction_t;

typedef struct {
    uint8_t direction;      /* 0: 直行, 1: 右转, 2: 左转 */
    uint8_t green;          /* 1: 绿灯, 0: 非绿灯 */
    uint8_t stop_flag;      /* 1: STOP 标志, 0: 无 */
    uint8_t all_black_flag; /* 1: 全黑, 0: 非全黑 */
    uint8_t obstacle_flag;  /* 1: 有障碍物, 0: 无障碍物 */
} perception_data_t;

void perception_init(void);
void perception_task(void);
perception_data_t *perception_get_data(void);

#endif
