#include <algorithm>
#include <raytiles/raytiles.h>
#include "fly.h"

int main() {
    SetTraceLogLevel(LOG_INFO);
    InitWindow(800, 600, "raytiles");

    raytiles::world_config world;
    raytiles::streaming_config streaming;
    raytiles::rendering_config rendering;
    raytiles::pool_config pool_conf;

    world.anchor_x_tile = 269;
    world.anchor_z_tile = 186;
    world.skirt_size = 0.5f;


    raytiles::streamer streamer(world, streaming, rendering, pool_conf);
    streamer.get_renderer().set_normals_scale(5.0f);

    Camera3D camera;
    camera.position = Vector3{3000.0f, 5000.0f, 3000.0f};
    camera.target = Vector3{10000.0f, 0.0f, 1000.0f};
    camera.up = Vector3{0.0f, 1.0f, 0.0f};
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    FreeCamera f(camera);
    streamer.get_renderer().set_fog_color(SKYBLUE);
    streamer.get_renderer().set_ambient_light(Color{200, 200, 200, 255});
    float sun = 1.0f;


    while (!WindowShouldClose()) {
        const auto dt = GetFrameTime();
        f.update(camera, dt);

        // UpdateCamera(&camera, CAMERA_ORBITAL);

        streamer.get_renderer().set_sun_direction(Vector3{0.1f, sun, 0.0f});
        streamer.update(camera);

        BeginDrawing();
        ClearBackground(SKYBLUE);

        BeginMode3D(camera);
        // draw the world around the camera
        streamer.draw(camera);
        streamer.debug_3d();
        EndMode3D();
        streamer.debug(camera);
        EndDrawing();

        if (IsKeyDown(KEY_LEFT_BRACKET)) sun -= dt * 0.5f;
        if (IsKeyDown(KEY_RIGHT_BRACKET)) sun += dt * 0.5f;
        sun = std::clamp(sun, -1.0f, 1.0f);
    }

    CloseWindow();
    return 0;
}
