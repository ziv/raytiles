#include <cstdlib>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>

#include "../raytiles.h"
#include "rlgl.h"
#ifdef __EMSCRIPTEN__
    #include <emscripten/emscripten.h>
#endif

static std::string required_env(const char *name, std::string_view label) {
    if (const char *value = std::getenv(name); value && *value) {
        return value;
    }
    throw std::runtime_error(std::format("missing {} token in options or environment variables", label));
}

int main() {
    SetTraceLogLevel(LOG_DEBUG);
    InitWindow(800, 600, "raytiles");

    // streamer configuration, set the anchor tiles (currently around greece)
    raytiles::config conf;
    conf.anchor_x_tile = 1179.0f;
    conf.anchor_z_tile = 797.0f;

    // pool configuration, set your mapbox token
    raytiles::pool_config pool_conf;
    pool_conf.download_threads = 2;
    pool_conf.token = required_env("MAPBOX_TOKEN", "mapbox token");

    // create the streamer with both configurations
    const raytiles::streamer streamer(conf, pool_conf);

    Camera3D camera;
    camera.position = Vector3{5000.0f, 3000.0f, 5000.0f};
    camera.target = Vector3{0.0f, 0.0f, 0.0f};
    camera.up = Vector3{0.0f, 1.0f, 0.0f};
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    rlSetClipPlanes(1, 100000);

    streamer.set_fog_color(SKYBLUE);

    auto update = [&]() {
        streamer.update(camera);
        BeginDrawing();
        ClearBackground(SKYBLUE);

        BeginMode3D(camera);
        // draw the world around the camera
        streamer.draw(camera);
        EndMode3D();
        // streamer.debug(camera);
        EndDrawing();

        const auto dt = GetFrameTime();
        if (IsKeyDown(KEY_W)) camera.position.z -= 1500.0f * dt;
        if (IsKeyDown(KEY_S)) camera.position.z += 1500.0f * dt;
        if (IsKeyDown(KEY_A)) camera.position.x -= 1500.0f * dt;
        if (IsKeyDown(KEY_D)) camera.position.x += 1500.0f * dt;
    };

#ifdef __EMSCRIPTEN__

    auto caller = [](void *arg) {
        auto *updateFunc = static_cast<decltype(update) *>(arg);
        (*updateFunc)();
    };
    emscripten_set_main_loop_arg(caller, &update, 0, 1);
#else

    while (!WindowShouldClose()) {
        update();
    }
#endif


    CloseWindow();
    return 0;
}
