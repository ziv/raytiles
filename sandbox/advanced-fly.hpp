#pragma once

#include "raylib.h"
#include "raymath.h"

namespace free_camera {
    inline float approach(const float current, const float target, const float maxDelta) {
        if (current < target) return current + maxDelta > target ? target : current + maxDelta;
        if (current > target) return current - maxDelta < target ? target : current - maxDelta;
        return current;
    }


    struct CameraInputs {
        float roll = 0.0f; // -1 to 1, left to right
        float pitch = 0.0f; // -1 to 1, up to down
        float yaw = 0.0f; // -1 to 1, left to right
    };

    struct CameraState {
        Vector3 forward;
        Vector3 up;
        Vector3 right;
        Vector3 linear_velocity;
        Vector3 angular_velocity;
        float velocity;
    };

    class AdvancedFreeCamera {
        float max_speed = 300.0f;
        Quaternion rotation_ = QuaternionIdentity();
        CameraInputs inputs_ = {0.0f, 0.0f, 0.0f};
        CameraState state_ = {
            .forward = {0.0f, 0.0f, 1.0f},
            .up = {0.0f, 1.0f, 0.0f},
            .right = {-1.0f, 0.0f, 0.0f},
            .linear_velocity = {0.0f, 0.0f, 0.0f},
            .angular_velocity = {0.0f, 0.0f, 0.0f},
            .velocity = 0.0f,
        };

    public:
        explicit AdvancedFreeCamera() noexcept = default;

        void update(Camera3D &cam, const float dt) noexcept {
            const auto speed = Vector3Length(state_.linear_velocity);
            const auto speed_raio = speed / max_speed;
            const auto control = std::max(speed_raio * speed_raio, 1.0f);

            constexpr auto stick_max = 3.0f;
            float targetRoll = 0.0f;
            if (IsKeyDown(KEY_RIGHT)) targetRoll = 1.0f;
            if (IsKeyDown(KEY_LEFT)) targetRoll = -1.0f;
            inputs_.roll = approach(inputs_.roll, targetRoll, stick_max * dt);

            float targetPitch = 0.0f;
            if (IsKeyDown(KEY_UP)) targetPitch = -1.0f;
            if (IsKeyDown(KEY_DOWN)) targetPitch = 1.0f;
            inputs_.pitch = approach(inputs_.pitch, targetPitch, stick_max * dt);

            float targetYaw = 0.0f;
            if (IsKeyDown(KEY_Q)) targetYaw = 1.0f;
            if (IsKeyDown(KEY_E)) targetYaw = -1.0f;
            inputs_.yaw = approach(inputs_.yaw, targetYaw, stick_max * dt);

            // angular velocity
            const Vector3 pitch_torque = Vector3Scale(state_.right, inputs_.pitch * control * dt);
            const Vector3 roll_torque = Vector3Scale(state_.forward, inputs_.roll * control * dt);
            const Vector3 yaw_torque = Vector3Scale(state_.up, inputs_.yaw * control * dt);

            state_.angular_velocity = state_.angular_velocity + pitch_torque + roll_torque + yaw_torque;
            state_.angular_velocity *= 0.999; // dumping

            if (const float angular_speed = Vector3Length(state_.angular_velocity); angular_speed > 0.0001f) {
                const float rotation_angle = angular_speed * dt;
                const auto rotation_axis = Vector3Normalize(state_.angular_velocity);
                const Quaternion delta_rotation = QuaternionFromAxisAngle(rotation_axis, rotation_angle);
                rotation_ = QuaternionMultiply(delta_rotation, rotation_);
                rotation_ = QuaternionNormalize(rotation_);

                state_.forward = Vector3Normalize(Vector3RotateByQuaternion({0.0f, 0.0f, 1.0f}, rotation_));
                state_.up = Vector3Normalize(Vector3RotateByQuaternion({0.0f, 1.0f, 0.0f}, rotation_));
                state_.right = Vector3Normalize(Vector3RotateByQuaternion({-1.0f, 0.0f, 0.0f}, rotation_));
            }


            // linear velocity

            if (IsKeyDown(KEY_EQUAL)) state_.velocity += 0.1;
            if (IsKeyDown(KEY_MINUS)) state_.velocity -= 0.1;
            state_.velocity = std::clamp(state_.velocity, 0.0f, max_speed);

            // camera update

            cam.position = cam.position + Vector3Scale(state_.forward, state_.velocity * dt);
            cam.target = cam.position + Vector3Scale(state_.forward, 2.0f);
            cam.up = state_.up;
        }
    };
}
