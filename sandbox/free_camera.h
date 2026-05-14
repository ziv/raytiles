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

struct FreeCameraConfig {
    float move_speed_mps = 200.0f; // base translation speed (m/s)
    float turn_speed_rps = 1.5f; // keyboard yaw/pitch rate (rad/s)
    float roll_speed_rps = 1.5f; // Q/E roll rate (rad/s)
    float mouse_sensitivity = 0.003f; // rad / pixel
    float fast_mul = 5.0f; // shift multiplier
    float slow_mul = 0.2f; // ctrl multiplier
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

    bool mouse_look() const noexcept { return mouse_look_; }
    float move_speed() const noexcept { return move_speed_; }

    void update(Camera3D &cam, float dt) noexcept {
        if (IsKeyPressed(KEY_TAB)) toggle_mouse_look();
        if (IsKeyPressed(KEY_R)) cam = initial_;

        float speed_mul = 10.0f;
        if (IsKeyDown(KEY_LEFT_SHIFT)) speed_mul *= cfg_.fast_mul;
        if (IsKeyDown(KEY_LEFT_CONTROL)) speed_mul *= cfg_.slow_mul;
        const float wheel = GetMouseWheelMove();
        if (wheel > 0) move_speed_ *= 1.25f;
        if (wheel < 0) move_speed_ /= 1.25f;
        const float frame_move = move_speed_ * speed_mul * dt;

        Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
        Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, cam.up));
        Vector3 up = Vector3Normalize(Vector3CrossProduct(right, fwd));

        Vector3 move{0, 0, 0};
        if (IsKeyDown(KEY_W)) move = Vector3Add(move, fwd);
        if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, fwd);
        if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
        if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);
        if (IsKeyDown(KEY_SPACE)) move = Vector3Add(move, up);
        if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_C)) move = Vector3Subtract(move, up);

        if (Vector3LengthSqr(move) > 0.0f) {
            move = Vector3Scale(Vector3Normalize(move), frame_move);
            cam.position = Vector3Add(cam.position, move);
            cam.target = Vector3Add(cam.target, move);
        }

        float yaw_d = 0.0f, pitch_d = 0.0f, roll_d = 0.0f;
        if (mouse_look_) {
            const Vector2 md = GetMouseDelta();
            yaw_d -= md.x * cfg_.mouse_sensitivity;
            pitch_d -= md.y * cfg_.mouse_sensitivity;
        }
        if (IsKeyDown(KEY_LEFT)) yaw_d += cfg_.turn_speed_rps * dt;
        if (IsKeyDown(KEY_RIGHT)) yaw_d -= cfg_.turn_speed_rps * dt;
        if (IsKeyDown(KEY_UP)) pitch_d += cfg_.turn_speed_rps * dt;
        if (IsKeyDown(KEY_DOWN)) pitch_d -= cfg_.turn_speed_rps * dt;
        if (IsKeyDown(KEY_Q)) roll_d -= cfg_.roll_speed_rps * dt;
        if (IsKeyDown(KEY_E)) roll_d += cfg_.roll_speed_rps * dt;

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
    FreeCameraConfig cfg_;
    Camera3D initial_;
    float move_speed_;
    bool mouse_look_ = false;
};
