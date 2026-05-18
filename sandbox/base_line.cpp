#include "raylib.h"
#include "raytiles/raytiles.h"

// baseline
// ----------------------------------------------
// before moving PNG decoding from main thread:
// ~2.2sec loading time
// after moving PNG decoding from main thread:
// ~1.6sec loading time
// after downloader refactoring
// ~1.5sec loading time
//

int main() {
    SetTraceLogLevel(LOG_WARNING);

    InitWindow(800, 600, "raytiles");
    double t = GetTime();

    raytiles::world_config world;
    raytiles::streaming_config streaming;
    raytiles::rendering_config rendering;
    raytiles::pool_config pool_conf;

    raytiles::streamer streamer(world, streaming, rendering, pool_conf);

    Camera3D camera;
    camera.position = Vector3{2000.0f, 5000.0f, 2000.0f};
    camera.target = Vector3{3000.0f, 4750.0f, 3000.0f};
    camera.up = Vector3{0.0f, 1.0f, 0.0f};
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // loading loop
    for (;;) {
        streamer.update(camera);
        if (!streamer.is_loading()) break;

        const auto loading = streamer.get_loading();
        BeginDrawing();
        ClearBackground(BLACK);
        DrawText(TextFormat("Loading... %.1f%%", loading * 100.0f), 350, 350, 50, WHITE);
        EndDrawing();
    }

    TraceLog(LOG_WARNING, "------------------ initial loading took %.2f seconds", GetTime() - t);
    CloseWindow();
    return 0;
}
