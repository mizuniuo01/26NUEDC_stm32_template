#ifndef AIM_CONFIG_H
#define AIM_CONFIG_H

/* 舵机正角对应方向；实机方向相反时在 0 和 1 之间翻转。 */
#define AIM_X_POSITIVE_ANGLE_MOVES_RIGHT 1U
#define AIM_Y_POSITIVE_ANGLE_MOVES_DOWN 0U

/* 云台上电及无目标时的绝对归位角，实机标定后修改。 */
#define AIM_GIMBAL_X_HOME_ANGLE_DEG 0.0F
#define AIM_GIMBAL_Y_HOME_ANGLE_DEG 0.0F

/* 摄像头测距几何标定量，实机标定后修改。 */
#define AIM_CAMERA_HORIZONTAL_Y_ANGLE_DEG 0.0F
#define AIM_CAMERA_LINK_REFERENCE_ANGLE_DEG 0.0F
#define AIM_GIMBAL_PIVOT_HEIGHT_MM 0.0F
#define AIM_CAMERA_LINK_LENGTH_MM 0.0F

#endif /* AIM_CONFIG_H */
