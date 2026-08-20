#pragma once
#include <raylib.h>

inline void rebase_large_world(Camera &camera, Vector3 &world_offset, const float rebase_threshold) {
    // Large-world rebase: keep the user-space camera close to the origin.
    // Whenever |camera.x| or |camera.z| exceeds the threshold, slide BOTH
    // the camera (position + target) AND the world_offset by the same
    // amount, preserving the absolute camera location (user - offset).
    // In a real game you must apply the same shift to every entity that
    // lives in user space (models, lights, particles, ...).
    if (camera.position.x > rebase_threshold) {
        camera.position.x -= rebase_threshold;
        camera.target.x -= rebase_threshold;
        world_offset.x -= rebase_threshold;
    } else if (camera.position.x < -rebase_threshold) {
        camera.position.x += rebase_threshold;
        camera.target.x += rebase_threshold;
        world_offset.x += rebase_threshold;
    }
    if (camera.position.z > rebase_threshold) {
        camera.position.z -= rebase_threshold;
        camera.target.z -= rebase_threshold;
        world_offset.z -= rebase_threshold;
    } else if (camera.position.z < -rebase_threshold) {
        camera.position.z += rebase_threshold;
        camera.target.z += rebase_threshold;
        world_offset.z += rebase_threshold;
    }
}


inline void loading_screen(raytiles::streamer &streamer, const Camera &camera, const Vector3 &world_offset) {
    for (;;) {
        streamer.update(camera, world_offset);
        if (!streamer.is_loading()) break;

        const auto loading = streamer.loading_progress();
        BeginDrawing();
        ClearBackground(BLACK);
        DrawText(TextFormat("Loading... %.1f%%", loading * 100.0f), 350, 350, 50, WHITE);
        EndDrawing();
    }
}

inline Camera get_perspective_camera(const Vector3 &position, const float fov = 60.0f) {
    Camera3D camera;
    camera.position = position;
    camera.target = {0.0f, 0.0f, 0.0f};
    camera.up = Vector3{0.0f, 1.0f, 0.0f};
    camera.fovy = fov;
    camera.projection = CAMERA_PERSPECTIVE;
    return camera;
}


inline void draw_controls_label() {
    DrawRectangle(5, 750, 600, 40, Fade(BLACK, 0.5f));
    DrawText("Controls:  K  to toggle labels,  L  to toggle wireframe,  +/-  throttle", 10, 760, 10, WHITE);
}

inline void draw_debug_data(const Camera &camera, const Vector3 &world_offset) {
    DrawRectangle(5, 10, 280, 80, Fade(BLACK, 0.5f));
    DrawText(TextFormat("user P %d %d %d",
                        static_cast<int>(camera.position.x),
                        static_cast<int>(camera.position.y),
                        static_cast<int>(camera.position.z)
             ), 10, 20, 20, WHITE);
    DrawText(TextFormat("offset %d %d %d",
                        static_cast<int>(world_offset.x),
                        static_cast<int>(world_offset.y),
                        static_cast<int>(world_offset.z)
             ), 10, 50, 20, WHITE);
}

inline void draw_crash_screen() {
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.5f));
    DrawText("You crashed! Press R to reset.", 200, 300, 30, WHITE);
}