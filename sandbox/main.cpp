#include <raytiles/raytiles.h>
#include "fly.h"
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#ifdef __EMSCRIPTEN__
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
#endif


int main() {
    SetTraceLogLevel(LOG_INFO);
    InitWindow(800, 600, "raytiles");
#ifdef __EMSCRIPTEN__
    InitStorage();
#endif
    double last_sync_time = GetTime();

    raytiles::world_config world;
    raytiles::streaming_config streaming;
    raytiles::rendering_config rendering;
    raytiles::pool_config pool;

    // streamer configuration, set the anchor tiles (currently around greece)
    world.anchor_x_tile = 294.0f;
    world.anchor_z_tile = 199.0f;

#ifdef __EMSCRIPTEN__
    pool.texture_cache_path = "/assets/t/{}/{}/{}.png";
    pool.heightmap_cache_path = "/assets/h/{}/{}/{}.png";
    pool.normals_cache_path = "/assets/n/{}/{}/{}.png";
#endif

    // create the streamer with all configurations
    raytiles::streamer streamer(world, streaming, rendering, pool);
    streamer.set_fog_color(SKYBLUE);

    Camera3D camera;
    camera.position = Vector3{3000.0f, 5000.0f, 3000.0f};
    camera.target = Vector3{10000.0f, 0.0f, 1000.0f};
    camera.up = Vector3{0.0f, 1.0f, 0.0f};
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    FreeCamera f(camera);

    auto update = [&] {
        f.update(camera, GetFrameTime());
        streamer.update(camera);

        BeginDrawing();
        ClearBackground(SKYBLUE);

        BeginMode3D(camera);
        // draw the world around the camera
        streamer.draw(camera);
        EndMode3D();
        EndDrawing();

        // sync file system in web env every 10 seconds
        if (GetTime() - last_sync_time > 10.0) {
            last_sync_time = GetTime();
#ifdef __EMSCRIPTEN__
            MAIN_THREAD_EM_ASM({
                console.log("Syncing to IndexedDB...");
                FS.syncfs(false, function(err) {
                    if (err) console.error("IDBFS Sync error:", err);
                    else console.log("IDBFS Sync successful!");
                });
            });
#endif
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
