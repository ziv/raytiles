/// Quick Start example for raytiles
/// @see https://github.com/ziv/raytiles/wiki/Quick-Start
///
#include "raylib.h"
#include "raytiles/raytiles.h"

int main() {
    InitWindow(800, 600, "Raytiles Quick Start");

    // 1.
    // seems like a lot of configuration,
    // but we will use the default values for most of them
    raytiles::world_config w;
    raytiles::streaming_config s;
    raytiles::rendering_config r;
    raytiles::pool_config p;

    // 2.
    // the only change we made is the anchor
    // point and the tile size we calculated
    // in the previous steps
    w.anchor_x_tile = 248;
    w.anchor_z_tile = 160;
    w.base_zoom_tile_size = 43769;

    // 3.
    // initialize the raytiles streamer
    raytiles::streamer streamer(w, s, r, p);

    // 4.
    // app camera
    Camera3D camera = {0};
    camera.up = (Vector3){0.0f, 1.0, 0.0f};
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // 5.
    // camera located at the origin, 5000m above
    // the ground, looking to the south-east
    camera.position = (Vector3){0.0f, 5000.0f, 0.0f};
    camera.target = (Vector3){-1000.0f, 4700.0f, -1000.0f};

    while (!WindowShouldClose()) {
        // 6.
        // do your staff here, for example, update
        // the camera position based on user input
        streamer.update(camera);

        BeginDrawing();
        ClearBackground(RAYWHITE);
        BeginMode3D(camera);
        // 7.
        // render the tiles around
        // the camera in 3D mode
        streamer.draw();
        EndMode3D();
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
