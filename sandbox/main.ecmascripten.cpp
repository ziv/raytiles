#ifdef __EMSCRIPTEN__
#include <algorithm>
#include <string>

#include "../raytiles.h"
#include <rlgl.h>
#include <raymath.h>

#include <emscripten/emscripten.h>
#include <emscripten.h>

bool g_storage_ready = false;

extern "C" EMSCRIPTEN_KEEPALIVE void MarkStorageReady() {
    g_storage_ready = true;
    TraceLog(LOG_INFO, "STORAGE: Cached tiles loaded from IndexedDB!");
}

void InitStorage() {
    EM_ASM(
        FS.mkdir('/assets');
        FS.mount(IDBFS, {}, '/assets');

        FS.syncfs(true, function(err)
        {
            if (err) console.error("Syncfs error:", err);
            _MarkStorageReady();
        });
    );
}


int main() {
    SetTraceLogLevel(LOG_DEBUG);
    InitWindow(800, 600, "raytiles");
    InitStorage();
    double last_sync_time = GetTime();


    // streamer configuration, set the anchor tiles (currently around greece)
    raytiles::config conf;
    conf.anchor_x_tile = 1179.0f; // somewhere at greece
    conf.anchor_z_tile = 797.0f;
    conf.max_zoom = 15;
    conf.height_scale = 3.0f;
    conf.skirt_size = 50;

    // pool configuration, set your mapbox token
    raytiles::pool_config pool_conf;
    pool_conf.download_threads = 2; // good enough

    pool_conf.texture_cache_path = "/assets/t/{}/{}/{}.png";
    pool_conf.heightmap_cache_path = "/assets/h/{}/{}/{}.png";

    // create the streamer with both configurations
    const raytiles::streamer streamer(conf, pool_conf);
    streamer.set_normals_scale(5.0f);

    Camera3D camera;
    camera.position = Vector3{3000.0f, 5000.0f, 3000.0f};
    camera.target = Vector3{0.0f, 0.0f, 0.0f};
    camera.up = Vector3{0.0f, 1.0f, 0.0f};
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    streamer.set_fog_color(SKYBLUE);
    streamer.set_ambient_light({200, 200, 200, 255});
    float sun = 1.0f;

    auto update = [&]() {
        streamer.update(camera);
        BeginDrawing();
        ClearBackground(SKYBLUE);

        BeginMode3D(camera);
        // draw the world around the camera
        streamer.draw(camera);
        streamer.debug_3d(camera);
        EndMode3D();
        streamer.debug(camera);
        EndDrawing();

        const auto dt = GetFrameTime();

        const auto last_pos = camera.position;

        if (IsKeyDown(KEY_W)) camera.position.z -= 1500.0f * dt;
        if (IsKeyDown(KEY_S)) camera.position.z += 1500.0f * dt;
        if (IsKeyDown(KEY_A)) camera.position.x -= 1500.0f * dt;
        if (IsKeyDown(KEY_D)) camera.position.x += 1500.0f * dt;
        if (IsKeyDown(KEY_DOWN)) camera.position.y -= 1500.0f * dt;
        if (IsKeyDown(KEY_UP)) camera.position.y += 1500.0f * dt;

        if (IsKeyDown(KEY_LEFT)) sun -= dt * 0.5f;
        if (IsKeyDown(KEY_RIGHT)) sun += dt * 0.5f;
        sun = std::clamp(sun, -1.0f, 1.0f);

        streamer.set_sun_direction(Vector3{sun, 0.1f, 0.0f});

        const auto move = camera.position - last_pos;
        // camera.target += move;


        // sync every 10 seconds
        if (GetTime() - last_sync_time > 10.0) {
            last_sync_time = GetTime();
            MAIN_THREAD_EM_ASM({
                console.log("Syncing to IndexedDB...");
                FS.syncfs(false, function(err) {
                    if (err) console.error("IDBFS Sync error:", err);
                    else console.log("IDBFS Sync successful!");












                });
            });
        }
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
