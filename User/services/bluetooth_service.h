#ifndef AUTO_BALL_CAR_USER_SERVICES_BLUETOOTH_SERVICE_H
#define AUTO_BALL_CAR_USER_SERVICES_BLUETOOTH_SERVICE_H /* 头文件保护 */

#include "status.h"
#include <stdint.h>

/* 蓝牙串口小程序协议适配与发送调度接口 */
void bluetooth_service_init(void);
status_code_t bluetooth_service_process(void);
status_code_t bluetooth_service_printf(const char *format, ...);
status_code_t bluetooth_service_display(int16_t x, int16_t y, const char *format, ...);
status_code_t bluetooth_service_clear_display(void);
status_code_t bluetooth_service_plot(const char *format, ...);

#endif /* AUTO_BALL_CAR_USER_SERVICES_BLUETOOTH_SERVICE_H */
