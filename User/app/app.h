#ifndef AUTO_BALL_CAR_USER_APP_APP_H
#define AUTO_BALL_CAR_USER_APP_APP_H /* 头文件保护 */

#include "status.h"

/* 产品应用生命周期和协作式主循环入口 */
status_code_t app_init(void);
void app_run_once(void);

#endif /* AUTO_BALL_CAR_USER_APP_APP_H */
