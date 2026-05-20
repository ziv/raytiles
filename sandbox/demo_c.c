/// @file demo_c.c
/// Minimal C demo for the raytiles C wrapper (see craytiles.h).
///
/// All configuration is left at defaults; the program simply opens a window,
/// shows a loading screen while the initial tile set fetches, then renders
/// the streamed world with a fixed camera. Press ESC to quit.
#include <raytiles/craytiles.h>
#include <raylib.h>

int main(void) {
    SetTraceLogLevel(LOG_INFO);
    InitWindow(800, 600, "raytiles - C demo");
    SetTargetFPS(60);

    RaytilesWorldConfig world = RaytilesWorldConfigDefault();
    RaytilesStreamingConfig streaming = RaytilesStreamingConfigDefault();
    RaytilesRenderingConfig rendering = RaytilesRenderingConfigDefault();
    RaytilesPoolConfig pool = RaytilesPoolConfigDefault();

    RaytilesStreamer *streamer = RaytilesStreamerCreate(&world, &streaming, &rendering, &pool);
    if (!streamer) {
        TraceLog(LOG_ERROR, "failed to create raytiles streamer");
        CloseWindow();
        return 1;
    }

    RaytilesStreamerSetFogColor(streamer, SKYBLUE);
    RaytilesStreamerSetAmbientLight(streamer, (Color){200, 200, 200, 255});
    RaytilesStreamerSetSunDirection(streamer, (Vector3){0.1f, 1.0f, 0.1f});

    Camera3D camera = {
        .position = {2000.0f, 5000.0f, 2000.0f},
        .target = {3000.0f, 4750.0f, 3000.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = 60.0f,
        .projection = CAMERA_PERSPECTIVE,
    };

    // initial-loading splash screen
    while (!WindowShouldClose()) {
        RaytilesStreamerUpdate(streamer, camera);
        if (!RaytilesStreamerIsLoading(streamer)) break;

        const float progress = RaytilesStreamerGetLoading(streamer) * 100.0f;
        BeginDrawing();
        ClearBackground(BLACK);
        DrawText(TextFormat("Loading... %.1f%%", progress), 280, 280, 30, WHITE);
        EndDrawing();
    }

    // main loop
    while (!WindowShouldClose()) {
        RaytilesStreamerUpdate(streamer, camera);

        BeginDrawing();
        ClearBackground(SKYBLUE);

        BeginMode3D(camera);
        RaytilesStreamerDraw(streamer, camera);
        EndMode3D();

        DrawText("raytiles C demo - ESC to quit", 10, 570, 20, RAYWHITE);
        EndDrawing();
    }

    RaytilesStreamerDestroy(streamer);
    CloseWindow();
    return 0;
}
