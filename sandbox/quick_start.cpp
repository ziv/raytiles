/// Quick Start example for raytiles
/// @see https://github.com/ziv/raytiles/wiki/Quick-Start
///
#include "raylib.h"
#include "raytiles/raytiles.h"
#include <rlgl.h>

int main() {
    InitWindow(800, 600, "Raytiles Quick Start");

    // 1.
    // anchor position (The Dolomites)
    constexpr double lat = 46.206889;
    constexpr double lon = 9.497194;

    // 2.
    // initialize the raytiles streamer with default configurations
    raytiles::streamer streamer(lat, lon);
    streamer.set_fog_color(SKYBLUE);

    // 3.
    // app camera
    Camera3D camera = {0};
    camera.up = (Vector3){0.0f, 1.0, 0.0f};
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // 4.
    // camera located at the origin, 5000m above
    // the ground, looking to the south-east
    camera.position = (Vector3){0.0f, 5000.0f, 0.0f};
    camera.target = (Vector3){-1000.0f, 4700.0f, -1000.0f};

    // 5.
    // loading loop, data is cached
    for (;;) {
        Vector3 world_offset = {0.0f, 0.0f, 0.0f};
        streamer.update(camera, world_offset);
        if (!streamer.is_loading()) break;

        const auto loading = streamer.loading_progress();
        BeginDrawing();
        ClearBackground(BLACK);
        DrawText(TextFormat("Loading... %.1f%%", loading * 100.0f), 350, 350, 50, WHITE);
        EndDrawing();
    }

    // 6.
    // make sure to see to the horizon
    rlSetClipPlanes(1.0, 400000.0);

    while (!WindowShouldClose()) {
        // 7.
        // do your staff here, for example, update
        // the camera position based on user input
        streamer.update(camera);

        BeginDrawing();
        ClearBackground(SKYBLUE);
        BeginMode3D(camera);
        // 8.
        // render the tiles around
        // the camera in 3D mode
        streamer.draw();
        EndMode3D();
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
