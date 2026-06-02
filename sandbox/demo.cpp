#include <algorithm>
#include <format>
#include <raytiles/raytiles.h>

#include "advanced-fly.hpp"
#include "fly.h"

static std::string required_env() {
    if (const char *value = std::getenv("MAPBOX_TOKEN"); value && *value) {
        return value;
    }
    throw std::runtime_error("missing Mapbox token in options or environment variables");
}

int main() {
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(800, 600, "raytiles");

    std::string token = required_env();

    raytiles::world_config world;
    raytiles::streaming_config streaming;
    raytiles::rendering_config rendering;
    raytiles::pool_config pool_conf;

    // pool_conf.texture_url = "https://api.mapbox.com/v4/mapbox.satellite/:zoom:/:x:/:y:.pngraw?access_token=" + token;
    pool_conf.download_threads = 8;

    // The Dolomites
    world.anchor_x_tile = 273;
    world.anchor_z_tile = 180;

    // The Grand Canyon
    // world.anchor_x_tile = 97;
    // world.anchor_z_tile = 200;

    // Adjust to fit your scene
    world.base_zoom_tile_size = 64000;
    rendering.skirt_drop = 1000.0f;
    world.skirt_overlap = {1.01f, 1.01f, 1.01f, 1.01f, 1.01f, 1.01f, 1.02f};


    raytiles::streamer streamer(world, streaming, rendering, pool_conf);

    Vector3 world_offset = {0.0f, 0.0f, 0.0f};
    constexpr float rebase_threshold = 4096.0f;

    auto absolute_to_user = [&](const Vector3 abs) {
        return Vector3Add(abs, world_offset);
    };

    Camera3D camera;
    camera.position = absolute_to_user({2000.0f, 5000.0f, 2000.0f});
    camera.target = absolute_to_user({3000.0f, 4750.0f, 3000.0f});
    camera.up = Vector3{0.0f, 1.0f, 0.0f};
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    free_camera::AdvancedFreeCamera adv_f{};
    Model tie = LoadModel("res/tie/scene.gltf");

    streamer.set_fog_color(SKYBLUE);
    streamer.set_ambient_light(Color{200, 200, 200, 255});
    float sun = 1.0f;
    bool wireframe = false;
    bool labels = false;
    bool crashed = false;

    // loading loop
    for (;;) {
        streamer.update(camera, world_offset);
        if (!streamer.is_loading()) break;

        const auto loading = streamer.get_loading();
        BeginDrawing();
        ClearBackground(BLACK);
        DrawText(TextFormat("Loading... %.1f%%", loading * 100.0f), 350, 350, 50, WHITE);
        EndDrawing();
    }

    while (!WindowShouldClose()) {
        const auto dt = GetFrameTime();

        const auto to_target = camera.target - camera.position;
        const auto forward = Vector3Normalize(to_target);
        // const auto right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
        // const auto up = Vector3Normalize(Vector3CrossProduct(right, forward));

        // rotate model 90 deg left on xz plane, so it faces forward
        // const Matrix rot = MatrixRotateY(-PI / 2);
        float angle = atan2f(forward.x, forward.z) * RAD2DEG;
        Vector3 rotationAxis = {0.0f, 1.0f, 0.0f};


        if (!crashed) adv_f.update(camera, dt);

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

        // Frame inputs are now stable for this frame -> hand them to the
        // streamer once. draw() and ground_height() will reuse these values.
        streamer.update(camera, world_offset);

        //r.set_sun_direction(Vector3{0.1f, sun, 0.0f});

        const auto h = streamer.ground_height(camera.position).value_or(0.0f);
        if (h > camera.position.y) crashed = true;

        BeginDrawing();
        ClearBackground(SKYBLUE);

        BeginMode3D(camera);
        // draw the world around the camera
        streamer.draw();
        const Vector3 model_pos = Vector3Add(camera.position, Vector3Scale(forward, 50.0f));

        DrawModelEx(tie, model_pos, rotationAxis, angle, {1.0f, 1.0f, 1.0f}, WHITE);
        if (wireframe) {
            streamer.draw_debug_3d();
        }
        EndMode3D();

        if (labels) {
            DrawRectangle(5, 5, 400, 100, Fade(BLACK, 0.5f));
            streamer.draw_debug_labels();
        }

        DrawRectangle(5, 550, 600, 40, Fade(BLACK, 0.5f));
        DrawText("Controls:  K  to toggle labels,  L  to toggle wireframe,  +/-  throttle", 10, 560, 10, WHITE);

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

        if (crashed) {
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.5f));
            DrawText("You crashed! Press R to reset.", 200, 300, 30, WHITE);
            if (IsKeyPressed(KEY_R)) {
                world_offset = {0.0f, 0.0f, 0.0f};
                camera.position = Vector3{2000.0f, 5000.0f, 2000.0f};
                camera.target = Vector3{3000.0f, 4750.0f, 3000.0f};
                crashed = false;
            }
        }
        EndDrawing();

        if (IsKeyDown(KEY_LEFT_BRACKET)) sun -= dt * 0.5f;
        if (IsKeyDown(KEY_RIGHT_BRACKET)) sun += dt * 0.5f;
        sun = std::clamp(sun, -1.0f, 1.0f);

        if (IsKeyPressed(KEY_L)) wireframe = !wireframe;
        if (IsKeyPressed(KEY_K)) labels = !labels;
    }

    CloseWindow();
    return 0;
}
