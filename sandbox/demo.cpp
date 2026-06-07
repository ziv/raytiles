#include <algorithm>
#include <format>
#include <raytiles/raytiles.h>
#include <rlgl.h>
#include "advanced-fly.hpp"
#include "fly.h"
#include <raytiles/rayskies.h>

#include "utils.h"

static std::string required_env() {
    if (const char *value = std::getenv("MAPBOX_TOKEN"); value && *value) {
        return value;
    }
    throw std::runtime_error("missing Mapbox token in options or environment variables");
}

int main() {
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(1024, 768, "raytiles");

    std::string token = required_env();

    raytiles::world_config word_conf;
    raytiles::streaming_config streaming_conf;
    raytiles::rendering_config rendering_conf;
    raytiles::pool_config pool_conf;

    // pool_conf.texture_url = "https://api.mapbox.com/v4/mapbox.satellite/:zoom:/:x:/:y:.pngraw?access_token=" + token;
    pool_conf.download_threads = 8;
    // rendering_conf.skirt_drop = 1000.0f;
    word_conf.skirt_overlap = {1.01f, 1.01f, 1.01f, 1.01f, 1.01f, 1.01f, 1.01f};


    // The Dolomites
    constexpr double lat = 46.206889;
    constexpr double lon = 9.497194;

    // My home
    // constexpr double lat = 32.11572;
    // constexpr double lon = 34.79118;

    // constexpr double lat = 32.73076;
    // constexpr double lon = 34.95166;

    // init tiles streamer
    raytiles::streamer streamer(lat, lon, word_conf, streaming_conf, rendering_conf, pool_conf);

    // init sky streamer
    raytiles::sky::sky_steamer sky;

    // runtime configuration
    sky.set_horizon_color(SKYBLUE);
    streamer.set_fog_color(SKYBLUE);
    streamer.set_ambient_light(Color{200, 200, 200, 255});

    Vector3 world_offset = {0.0f, 0.0f, 0.0f};
    free_camera::AdvancedFreeCamera adv_f{};
    Camera3D camera = get_perspective_camera(streamer.get_initial_position(5000.0f));
    Model tie = LoadModel("res/tie/scene.gltf");

    float sun = 1.0f;
    bool wireframe = false;
    bool labels = false;
    bool crashed = false;

    // loading resources (and cache them)
    loading_screen(streamer, camera, world_offset);

    // make sure we'll see the horizon
    rlSetClipPlanes(streaming_conf.near_plane, streaming_conf.far_plane);

    while (!WindowShouldClose()) {
        constexpr float rebase_threshold = 4096.0f;
        const auto dt = GetFrameTime();

        // update camera with fly handler as long not crashed
        if (!crashed) adv_f.update(camera, dt);

        rebase_large_world(camera, world_offset, rebase_threshold);

        streamer.update(camera, world_offset);

        // crash check
        // todo for performance, can be check only if there is a movement
        if (streamer.ground_height(camera.position).value_or(0.0f) > camera.position.y) crashed = true;

        BeginDrawing();
        ClearBackground(SKYBLUE);

        BeginMode3D(camera);
        // draw the sky & the world around the camera
        sky.draw(camera.position);
        streamer.draw();

        // reference model
        const auto forward = Vector3Normalize(camera.target - camera.position);
        const Vector3 model_pos = Vector3Add(camera.position, Vector3Scale(forward, 50.0f));
        DrawModel(tie, model_pos, 1.0f, WHITE);

        if (wireframe) streamer.draw_debug_3d();
        EndMode3D();

        // draw zoom label above the tiles
        if (labels) streamer.draw_debug_labels();

        draw_controls_label();
        draw_debug_data(camera, world_offset);

        if (crashed) draw_crash_screen();
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
