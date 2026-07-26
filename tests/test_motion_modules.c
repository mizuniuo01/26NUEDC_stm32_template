/**
 * @file test_motion_modules.c
 * @brief 主机侧验证串级 PID 和运动规划的关键行为。
 */
#include <assert.h>
#include <math.h>
#include <stddef.h>

#include "cascaded_pid.h"
#include "motion_planner.h"

static void assert_near(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.001f);
}

static cascaded_pid_config_t make_pid_config(void)
{
    const pid_param_t heading = {
        .kp = 1.0f,
        .ki = 0.0f,
        .kd = 0.0f,
        .out_max = 20.0f,
        .integral_max = 10.0f,
    };
    const pid_param_t speed = {
        .kp = 1.0f,
        .ki = 0.0f,
        .kd = 0.0f,
        .out_max = 100.0f,
        .integral_max = 10.0f,
    };
    const cascaded_pid_config_t config = {
        .heading_pid = heading,
        .left_speed_pid = speed,
        .right_speed_pid = speed,
        .speed_target_limit = 50.0f,
        .differential_limit = 20.0f,
    };

    return config;
}

static void test_cascaded_pid(void)
{
    const cascaded_pid_config_t config = make_pid_config();
    cascaded_pid_t controller = {0};
    cascaded_pid_output_t output = {0};
    cascaded_pid_input_t input = {
        .base_speed = 30.0f,
        .target_angle_deg = 10.0f,
        .actual_angle_deg = 0.0f,
        .left_speed = 0.0f,
        .right_speed = 0.0f,
        .external_differential = 15.0f,
        .is_angle_enabled = 1U,
    };

    assert(cascaded_pid_init(&controller, &config) == CASCADED_PID_STATUS_OK);
    assert(cascaded_pid_step(&controller, &input, &output) ==
           CASCADED_PID_STATUS_OK);
    assert_near(output.heading_output, 10.0f);
    assert_near(output.differential, 20.0f);
    assert_near(output.left_speed_target, 10.0f);
    assert_near(output.right_speed_target, 50.0f);
    assert_near(output.left_output, 10.0f);
    assert_near(output.right_output, 50.0f);

    input.is_angle_enabled = 0U;
    input.external_differential = 0.0f;
    assert(cascaded_pid_step(&controller, &input, &output) ==
           CASCADED_PID_STATUS_OK);
    assert_near(output.heading_output, 0.0f);
    assert_near(output.left_speed_target, 30.0f);
    assert_near(output.right_speed_target, 30.0f);

    assert(cascaded_pid_reset(&controller) == CASCADED_PID_STATUS_OK);
    input.base_speed = 0.0f;
    input.target_angle_deg = -180.0f;
    input.actual_angle_deg = 179.0f;
    input.is_angle_enabled = 1U;
    assert(cascaded_pid_step(&controller, &input, &output) ==
           CASCADED_PID_STATUS_OK);
    assert_near(output.heading_output, 1.0f);
    input.actual_angle_deg = -179.0f;
    assert(cascaded_pid_step(&controller, &input, &output) ==
           CASCADED_PID_STATUS_OK);
    assert_near(output.heading_output, -1.0f);

    assert(cascaded_pid_step(NULL, &input, &output) ==
           CASCADED_PID_STATUS_INVALID_ARGUMENT);
}

static void test_motion_planner(void)
{
    const motion_planner_config_t config = {
        .encoder_counts_per_mm = 2.0f,
        .rotate_dead_zone_deg = 3.0f,
    };
    const motion_planner_normal_command_t normal = {
        .base_speed = 5.0f,
        .target_angle_deg = 0.0f,
        .external_differential = 0.0f,
        .is_angle_enabled = 1U,
    };
    const motion_planner_feedback_t feedback = {
        .left_encoder_delta = 5.0f,
        .right_encoder_delta = 5.0f,
        .actual_angle_deg = 0.0f,
    };
    motion_planner_t planner = {0};
    motion_planner_command_t command = {0};
    motion_planner_state_t state;
    float remaining_mm;

    assert(motion_planner_init(&planner, &config, &normal) == STATUS_OK);
    assert(motion_planner_start_move(&planner, 10.0f, 4.0f) == STATUS_OK);
    assert(motion_planner_step(&planner, &feedback, &command) == STATUS_OK);
    assert_near(command.base_speed, 4.0f);
    assert(motion_planner_get_state(&planner, &state) == STATUS_OK);
    assert(state == MOTION_PLANNER_STATE_MOVE);

    assert(motion_planner_step(&planner, &feedback, &command) == STATUS_OK);
    assert(motion_planner_get_remaining_mm(&planner, &remaining_mm) == STATUS_OK);
    assert_near(remaining_mm, 7.5f);

    assert(motion_planner_replan_remaining(&planner, 2.0f) == STATUS_OK);
    assert(motion_planner_step(&planner, &feedback, &command) == STATUS_OK);
    assert(motion_planner_step(&planner, &feedback, &command) == STATUS_OK);
    assert(motion_planner_get_state(&planner, &state) == STATUS_OK);
    assert(state == MOTION_PLANNER_STATE_IDLE);
    assert_near(command.base_speed, 5.0f);

    assert(motion_planner_start_rotate(&planner, 90.0f, 8.0f) == STATUS_OK);
    {
        const motion_planner_feedback_t rotate_feedback = {
            .left_encoder_delta = 0.0f,
            .right_encoder_delta = 0.0f,
            .actual_angle_deg = 88.0f,
        };
        assert(motion_planner_step(&planner, &rotate_feedback, &command) ==
               STATUS_OK);
    }
    assert(motion_planner_get_state(&planner, &state) == STATUS_OK);
    assert(state == MOTION_PLANNER_STATE_IDLE);
    assert_near(command.target_angle_deg, 90.0f);

    assert(motion_planner_start_move(&planner, 10.0f, 4.0f) == STATUS_OK);
    assert(motion_planner_halt(&planner) == STATUS_OK);
    assert(motion_planner_get_command(&planner, &command) == STATUS_OK);
    assert_near(command.base_speed, 0.0f);
    assert(motion_planner_step(&planner, &feedback, &command) == STATUS_OK);
    assert_near(command.base_speed, 0.0f);
    assert(motion_planner_step(&planner, &feedback, &command) == STATUS_OK);
    assert_near(command.base_speed, 4.0f);

    assert(motion_planner_cancel(&planner) == STATUS_OK);
    assert(motion_planner_get_state(&planner, &state) == STATUS_OK);
    assert(state == MOTION_PLANNER_STATE_IDLE);
}

int main(void)
{
    test_cascaded_pid();
    test_motion_planner();
    return 0;
}
