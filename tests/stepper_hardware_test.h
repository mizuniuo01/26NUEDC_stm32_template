#ifndef AUTO_BALL_CAR_TESTS_STEPPER_HARDWARE_TEST_H
#define AUTO_BALL_CAR_TESTS_STEPPER_HARDWARE_TEST_H /* 头文件保护 */

#include "status.h"

/* 蓝牙触发的 ZDT X42S 硬件测试生命周期接口 */
status_code_t stepper_hardware_test_init(void);
status_code_t stepper_hardware_test_process(void);

#endif /* AUTO_BALL_CAR_TESTS_STEPPER_HARDWARE_TEST_H */
