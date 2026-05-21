#include <algorithm>
#include <cstdio>
#include <format>
#include <vector>
#include <raytiles/raytiles.h>

#include "advanced-fly.hpp"
#include "fly.h"

static std::string required_env(const char *name, std::string_view label) {
    if (const char *value = std::getenv(name); value && *value) {
        return value;
    }
    throw std::runtime_error(std::format("missing {} token in options or environment variables", label));
}

Vector3 get_spline_point(const Vector3 p0, const Vector3 p1, const Vector3 p2, const Vector3 p3, const float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;

    Vector3 result;
    result.x = 0.5f * (2.0f * p1.x + (-p0.x + p2.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 + (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) *
                       t3);
    result.y = 0.5f * (2.0f * p1.y + (-p0.y + p2.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 + (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) *
                       t3);
    result.z = 0.5f * (2.0f * p1.z + (-p0.z + p2.z) * t + (2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2 + (-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) *
                       t3);

    return result;
}

int main() {
    // Autopilot - Star Ward Scene in the Grand Canyon
    std::vector<Vector3> path = {
        {2468.0f, 1339.0f, 380.0f},
        {1525.0f, 1054.0f, 1230.0f},
        {509.0f, 963.0f, 2326.0f},
        {-596.0f, 1012.0f, 2768.0f},
        {-1493.0f, 1066.0f, 3736.0f},
        {-2157.0f, 998.0f, 4946.0f},
        {-2736.0f, 1036.0f, 5688.0f},
        {-2890.0f, 1121.0f, 6991.0f},
        {-3367.0f, 1231.0f, 7762.0f},
        {-13233.0f, 4759.0f, 20554.0f},
        {-15447.0f, 5597.0f, 23428.0f},
    };
    float flight_progress = 0.0f;
    int step = 0;
    bool auto_pilot = false;


    SetTraceLogLevel(LOG_WARNING);
    InitWindow(800, 600, "raytiles");

    std::string token = required_env("MAPBOX_TOKEN", "Mapbox");

    raytiles::world_config world;
    raytiles::streaming_config streaming;
    raytiles::rendering_config rendering;
    raytiles::pool_config pool_conf;

    pool_conf.texture_url = "https://api.mapbox.com/v4/mapbox.satellite/:zoom:/:x:/:y:.pngraw?access_token=" + token;
    pool_conf.download_threads = 8;

    // everest
    // world.anchor_x_tile = 373;
    // world.anchor_z_tile = 214;

    // scotland
    // world.anchor_x_tile = 248;
    // world.anchor_z_tile = 160;
    // world.base_zoom_tile_size = 43769;

    // The Dolomites
    // world.anchor_x_tile = 273;
    // world.anchor_z_tile = 180;


    // The Grand Canyon
    world.anchor_x_tile = 97;
    world.anchor_z_tile = 200;

    // Norway
    // world.anchor_x_tile = 265;
    // world.anchor_z_tile = 143;

    // Grand Teton
    // world.anchor_x_tile = 98;
    // world.anchor_z_tile = 186;

    // Crete
    // world.anchor_x_tile = 292;
    // world.anchor_z_tile = 202;


    // Adjust to fit your scene
    rendering.skirt_drop = 1000.0f;
    world.skirt_overlap = {
        1.01f, 1.01f, 1.01f, 1.01f, 1.01f, 1.01f, 1.02f
    };



    raytiles::streamer streamer(world, streaming, rendering, pool_conf);
    // streamer.set_normals_scale(5.0f);

    // ------- Large-world shifting state -------
    // Convention: absolute = user - world_offset, equivalently user = absolute + world_offset.
    // The `path[]` waypoints below are absolute coords (authored at offset = 0).
    // We keep `camera.position` in user space (small floats) and accumulate the
    // shift in `world_offset`. Each frame we test the user-space camera against
    // `rebase_threshold` and, when crossed, shift the user frame back toward
    // the origin while shifting `world_offset` by the same amount so the
    // absolute camera (user - offset) is unchanged.
    Vector3 world_offset = {0.0f, 0.0f, 0.0f};
    constexpr float rebase_threshold = 4096.0f;

    auto absolute_to_user = [&](Vector3 abs) {
        return Vector3Add(abs, world_offset);
    };

    Camera3D camera;
    camera.position = absolute_to_user({2000.0f, 5000.0f, 2000.0f});
    camera.target = absolute_to_user({3000.0f, 4750.0f, 3000.0f});
    camera.up = Vector3{0.0f, 1.0f, 0.0f};
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // FreeCamera f(camera);
    free_camera::AdvancedFreeCamera adv_f{};

    // Model x_wing = LoadModel("res/x-wing/scene.gltf");
    Model tie = LoadModel("res/tie/scene.gltf");
    // x_wing.transform = MatrixMultiply(MatrixRotateX(10.0f * DEG2RAD), MatrixRotateY(15.0f * DEG2RAD));

    streamer.set_fog_color(SKYBLUE);
    // streamer.set_ambient_light(Color{200, 200, 200, 255});
    float sun = 1.0f;
    bool wireframe = true;
    bool labels = true;
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

        if (auto_pilot && !crashed) {
            float step_time = 5.0f;
            flight_progress += dt / step_time;

            if (flight_progress >= 1.0f) {
                flight_progress -= 1.0f;
                step++;
                if (step >= path.size() - 3) step = 0;
            }
            // Path waypoints are absolute; convert to user space using the
            // current world_offset (user = absolute + offset).
            camera.position = absolute_to_user(get_spline_point(
                path[step % path.size()],
                path[(step + 1) % path.size()],
                path[(step + 2) % path.size()],
                path[(step + 3) % path.size()],
                flight_progress
            ));

            // look ahead...
            camera.target = absolute_to_user(get_spline_point(
                path[(step + 1) % path.size()],
                path[(step + 2) % path.size()],
                path[(step + 3) % path.size()],
                path[(step + 4) % path.size()],
                flight_progress + 0.2f
            ));
        }

        const auto to_target = camera.target - camera.position;
        const auto forward = Vector3Normalize(to_target);
        // const auto right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
        // const auto up = Vector3Normalize(Vector3CrossProduct(right, forward));

        // rotate model 90 deg left on xz plane, so it faces forward
        // const Matrix rot = MatrixRotateY(-PI / 2);
        float angle = atan2f(forward.x, forward.z) * RAD2DEG;
        Vector3 rotationAxis = {0.0f, 1.0f, 0.0f};


        if (!crashed) {
            // f.update(camera, dt);
            adv_f.update(camera, dt);
        }

        // Large-world rebase: keep the user-space camera close to the origin.
        // Whenever |camera.x| or |camera.z| exceeds the threshold, slide BOTH
        // the camera (position + target) AND the world_offset by the same
        // amount, preserving the absolute camera location (user - offset).
        // In a real game you must apply the same shift to every entity that
        // lives in user space (models, lights, particles, ...).
        auto rebase_axis = [&](const char axis, float &user, float &target, float &off) {
            if (user > rebase_threshold) {
                user -= rebase_threshold;
                target -= rebase_threshold;
                off -= rebase_threshold;
                std::printf("[rebase] %c -= %.0f  user=%.1f offset=%.1f\n",
                            axis, rebase_threshold, user, off);
            } else if (user < -rebase_threshold) {
                user += rebase_threshold;
                target += rebase_threshold;
                off += rebase_threshold;
                std::printf("[rebase] %c += %.0f  user=%.1f offset=%.1f\n",
                            axis, rebase_threshold, user, off);
            }
        };
        rebase_axis('x', camera.position.x, camera.target.x, world_offset.x);
        rebase_axis('z', camera.position.z, camera.target.z, world_offset.z);

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

        DrawModelEx(tie, model_pos, rotationAxis, angle, {2.0f, 2.0f, 2.0f}, WHITE);
        // if (wireframe) {
        //     streamer.debug_3d();
        // }
        EndMode3D();

        // if (labels) {
        //     DrawRectangle(5, 5, 400, 100, Fade(BLACK, 0.5f));
        //     streamer.debug(camera);
        // }

        // DrawRectangle(5, 550, 600, 40, Fade(BLACK, 0.5f));
        // DrawText("Controls: K to toggle labels, L to toggle wireframe", 10, 560, 20, WHITE);

        DrawRectangle(5, 50, 280, 80, Fade(BLACK, 0.5f));
        DrawText(TextFormat("user P %d %d %d",
                            static_cast<int>(camera.position.x),
                            static_cast<int>(camera.position.y),
                            static_cast<int>(camera.position.z)
                 ), 10, 60, 20, WHITE);
        DrawText(TextFormat("offset %d %d %d",
                            static_cast<int>(world_offset.x),
                            static_cast<int>(world_offset.y),
                            static_cast<int>(world_offset.z)
                 ), 10, 90, 20, WHITE);

        if (crashed) {
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.5f));
            DrawText("You crashed! Press R to reset.", 200, 300, 30, WHITE);
            if (IsKeyPressed(KEY_R)) {
                world_offset = {0.0f, 0.0f, 0.0f};
                camera.position = Vector3{2000.0f, 5000.0f, 2000.0f};
                camera.target = Vector3{3000.0f, 4750.0f, 3000.0f};
                // f = FreeCamera(camera);
                crashed = false;
            }
        }
        EndDrawing();

        if (IsKeyDown(KEY_LEFT_BRACKET)) sun -= dt * 0.5f;
        if (IsKeyDown(KEY_RIGHT_BRACKET)) sun += dt * 0.5f;
        sun = std::clamp(sun, -1.0f, 1.0f);

        if (IsKeyPressed(KEY_L)) wireframe = !wireframe;
        if (IsKeyPressed(KEY_K)) labels = !labels;
        if (IsKeyPressed(KEY_P)) auto_pilot = !auto_pilot;
    }

    CloseWindow();
    return 0;
}
