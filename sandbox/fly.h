#pragma once

// Free-fly camera controller for raylib Camera3D.
//
// Conventions assumed by this controller:
//   - Right-handed local frame.
//   - cam.up is the world-up direction at the moment of the call (does not
//     have to be (0,1,0) — for raylib local frame it's typically (0,1,0)
//     but the controller derives a fresh basis every frame).
//
// Controls (when `enable` is true):
//   WASD               translate forward / back / left / right
//   Space              translate along camera up
//   C / Left-Alt       translate along camera down
//   Mouse (when locked) yaw + pitch
//   Arrow keys         yaw + pitch
//   Q / E              roll
//   Shift              5x speed
//   Ctrl               0.2x speed
//   Mouse wheel        scale base move speed (x1.25 per click)
//   TAB                toggle mouse-look (capture / release cursor)
//   R                  reset to the camera passed at construction
//

#include "raylib.h"
#include "raymath.h"
#include <cmath>

struct FreeCameraConfig {
    float move_speed_mps = 200.0f; // base translation speed (m/s)
    float turn_speed_rps = 1.5f; // keyboard yaw/pitch rate (rad/s)
    float roll_speed_rps = 1.5f; // Q/E roll rate (rad/s)
    float mouse_sensitivity = 0.003f; // rad / pixel
    float fast_mul = 5.0f; // shift multiplier
    float slow_mul = 0.2f; // ctrl multiplier

    // Smoothing time constants (seconds). Larger = smoother / slower to react.
    // 0 disables smoothing on that channel. These are "time to reach ~63% of
    // the target"; ~95% is reached after 3x the constant.
    float move_smooth_time = 0.35f; // translation easing
    float turn_smooth_time = 0.35f; // keyboard yaw / pitch / roll easing
    float mouse_smooth_time = 0.18f; // mouse-look easing (kept shorter to stay responsive)
};

class FreeCamera {
public:
    explicit FreeCamera(const Camera3D &initial,
                        const FreeCameraConfig &cfg = {}) noexcept
        : cfg_(cfg), initial_(initial), move_speed_(cfg.move_speed_mps) {
    }

    void enter_mouse_look() noexcept {
        mouse_look_ = true;
        DisableCursor();
    }

    void exit_mouse_look() noexcept {
        mouse_look_ = false;
        EnableCursor();
    }

    void toggle_mouse_look() noexcept {
        if (mouse_look_) exit_mouse_look();
        else enter_mouse_look();
    }

    [[nodiscard]] bool mouse_look() const noexcept { return mouse_look_; }
    [[nodiscard]] float move_speed() const noexcept { return move_speed_; }

    void update(Camera3D &cam, float dt) noexcept {
        if (IsKeyPressed(KEY_TAB)) toggle_mouse_look();
        if (IsKeyPressed(KEY_R)) {
            cam = initial_;
            velocity_ = {0, 0, 0};
            yaw_rate_ = pitch_rate_ = roll_rate_ = 0.0f;
        }

        float speed_mul = 10.0f;
        if (IsKeyDown(KEY_LEFT_SHIFT)) speed_mul *= cfg_.fast_mul;
        if (IsKeyDown(KEY_LEFT_CONTROL)) speed_mul *= cfg_.slow_mul;
        const float wheel = GetMouseWheelMove();
        if (wheel > 0) move_speed_ *= 1.25f;
        if (wheel < 0) move_speed_ /= 1.25f;
        const float target_speed = move_speed_ * speed_mul;

        Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
        Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, cam.up));
        Vector3 up = Vector3Normalize(Vector3CrossProduct(right, fwd));

        // Build the target velocity from current key state.
        Vector3 input{0, 0, 0};
        if (IsKeyDown(KEY_W)) input = Vector3Add(input, fwd);
        if (IsKeyDown(KEY_S)) input = Vector3Subtract(input, fwd);
        if (IsKeyDown(KEY_D)) input = Vector3Add(input, right);
        if (IsKeyDown(KEY_A)) input = Vector3Subtract(input, right);
        if (IsKeyDown(KEY_SPACE)) input = Vector3Add(input, up);
        if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_C)) input = Vector3Subtract(input, up);

        Vector3 target_velocity{0, 0, 0};
        if (Vector3LengthSqr(input) > 0.0f) {
            target_velocity = Vector3Scale(Vector3Normalize(input), target_speed);
        }

        // Exponential smoothing toward target velocity. Frame-rate independent.
        const float t_move = smooth_factor(cfg_.move_smooth_time, dt);
        velocity_.x = lerp(velocity_.x, target_velocity.x, t_move);
        velocity_.y = lerp(velocity_.y, target_velocity.y, t_move);
        velocity_.z = lerp(velocity_.z, target_velocity.z, t_move);

        if (Vector3LengthSqr(velocity_) > 1e-6f) {
            const Vector3 step = Vector3Scale(velocity_, dt);
            cam.position = Vector3Add(cam.position, step);
            cam.target = Vector3Add(cam.target, step);
        }

        // Keyboard angular target rates (rad/s). Mouse is treated separately
        // because it's a per-frame delta, not a held rate, and benefits from a
        // much shorter time constant to stay responsive.
        float target_yaw_rate = 0.0f, target_pitch_rate = 0.0f, target_roll_rate = 0.0f;
        if (IsKeyDown(KEY_LEFT)) target_yaw_rate += cfg_.turn_speed_rps;
        if (IsKeyDown(KEY_RIGHT)) target_yaw_rate -= cfg_.turn_speed_rps;
        if (IsKeyDown(KEY_UP)) target_pitch_rate -= cfg_.turn_speed_rps;
        if (IsKeyDown(KEY_DOWN)) target_pitch_rate += cfg_.turn_speed_rps;
        if (IsKeyDown(KEY_Q)) target_roll_rate -= cfg_.roll_speed_rps;
        if (IsKeyDown(KEY_E)) target_roll_rate += cfg_.roll_speed_rps;

        const float t_turn = smooth_factor(cfg_.turn_smooth_time, dt);
        yaw_rate_ = lerp(yaw_rate_, target_yaw_rate, t_turn);
        pitch_rate_ = lerp(pitch_rate_, target_pitch_rate, t_turn);
        roll_rate_ = lerp(roll_rate_, target_roll_rate, t_turn);

        float yaw_d = yaw_rate_ * dt;
        float pitch_d = pitch_rate_ * dt;
        float roll_d = roll_rate_ * dt;

        // Mouse-look: smooth the per-frame pixel delta (converted to rad) toward
        // 0 so that motion fades out for a couple of frames after the user
        // stops, instead of cutting off sharply.
        if (mouse_look_) {
            const auto [x, y] = GetMouseDelta();
            const float target_mx = -x * cfg_.mouse_sensitivity;
            const float target_my = -y * cfg_.mouse_sensitivity;
            const float t_mouse = smooth_factor(cfg_.mouse_smooth_time, dt);
            mouse_yaw_ = lerp(mouse_yaw_, target_mx, t_mouse);
            mouse_pitch_ = lerp(mouse_pitch_, target_my, t_mouse);
        } else {
            mouse_yaw_ = mouse_pitch_ = 0.0f;
        }
        yaw_d += mouse_yaw_;
        pitch_d += mouse_pitch_;

        if (yaw_d != 0.0f || pitch_d != 0.0f || roll_d != 0.0f) {
            Matrix Ry = MatrixRotate(up, yaw_d);
            Matrix Rp = MatrixRotate(right, pitch_d);
            Matrix Rr = MatrixRotate(fwd, roll_d);
            Matrix R = MatrixMultiply(MatrixMultiply(Ry, Rp), Rr);
            fwd = Vector3Transform(fwd, R);
            up = Vector3Transform(up, R);
            cam.target = Vector3Add(cam.position, fwd);
            cam.up = up;
        }
    }

private:
    // Frame-rate independent exponential smoothing factor.
    // tau is the "time constant" — after ~tau seconds the lerp covers ~63%.
    // tau<=0 disables smoothing (returns 1, i.e. snap to target).
    static float smooth_factor(const float tau, const float dt) noexcept {
        if (tau <= 0.0f || dt <= 0.0f) return 1.0f;
        return 1.0f - std::exp(-dt / tau);
    }

    static float lerp(const float a, const float b, const float t) noexcept {
        return a + (b - a) * t;
    }

    FreeCameraConfig cfg_;
    Camera3D initial_;
    float move_speed_;
    bool mouse_look_ = false;

    // Smoothed state, persisted across frames.
    Vector3 velocity_{0, 0, 0};
    float yaw_rate_ = 0.0f;
    float pitch_rate_ = 0.0f;
    float roll_rate_ = 0.0f;
    float mouse_yaw_ = 0.0f;
    float mouse_pitch_ = 0.0f;
};