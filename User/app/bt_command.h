#ifndef BT_COMMAND_H
#define BT_COMMAND_H

/* 蓝牙调参步长（浮点常量，必须用宏） */
#define BLT_STEP_SPD_KP 1.0f   /* 速度环 KP 步长 */
#define BLT_STEP_SPD_KI 0.5f   /* 速度环 KI 步长 */
#define BLT_STEP_SPD_KD 1.0f   /* 速度环 KD 步长 */
#define BLT_STEP_ANG_KP 0.1f   /* 角度环 KP 步长 */
#define BLT_STEP_ANG_KI 0.01f  /* 角度环 KI 步长 */
#define BLT_STEP_ANG_KD 1.0f   /* 角度环 KD 步长 */
#define BLT_STEP_TARGET_ANG 5.0f   /* 目标角度步长（度） */
#define BLT_STEP_ROTATE_ANG 5.0f   /* 旋转角度步长（度） */

/* 蓝牙调参步长（整数常量） */
typedef enum {
    BLT_STEP_BASE_SPD = 1,    /* 基础速度步长（count/10ms） */
    BLT_STEP_MOVE_DIST = 10,   /* 移动距离步长（mm，即 1cm） */
} bt_step_int_t;

void on_led1_toggle_cmd(void);

void on_spd_kp_up(void);
void on_spd_kp_down(void);
void on_spd_ki_up(void);
void on_spd_ki_down(void);
void on_spd_kd_up(void);
void on_spd_kd_down(void);

void on_ang_kp_up(void);
void on_ang_kp_down(void);
void on_ang_ki_up(void);
void on_ang_ki_down(void);
void on_ang_kd_up(void);
void on_ang_kd_down(void);

void on_base_spd_up(void);
void on_base_spd_down(void);
void on_target_ang_up(void);
void on_target_ang_down(void);

void on_move_dist_up(void);
void on_move_dist_down(void);
void on_rotate_ang_up(void);
void on_rotate_ang_down(void);
void on_move_start(void);
void on_rotate_start(void);
void on_rotate_right(void);

void on_ctrl_toggle(void);

#endif
