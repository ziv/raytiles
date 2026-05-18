#include <algorithm>
#include <format>
#include <raytiles/raytiles.h>
#include "fly.h"

static std::string required_env(const char *name, std::string_view label) {
    if (const char *value = std::getenv(name); value && *value) {
        return value;
    }
    throw std::runtime_error(std::format("missing {} token in options or environment variables", label));
}

Vector3 GetSplinePoint(const Vector3 p0, const Vector3 p1, const Vector3 p2, const Vector3 p3, float t) {
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
    // std::vector<Vector3> path = {
    //     {0.0f, 5000.0f, 0.0f},
    //     {500.0f, 5000.0f, 0.0f},
    //     {500.0f, 5000.0f, 500.0f},
    //     {0.0f, 5000.0f, 500.0f},
    //     {0.0f, 5000.0f, 500.0f},
    //     {0.0f, 5000.0f, 500.0f},
    // };
    // float flight_progress = 0.0f;
    // float flight_speed = 0.1f;

    SetTraceLogLevel(LOG_INFO);
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
    // rendering.height_scale = 1.5f;
    // rendering.skirt_drop = 1000.0f;

    // The Dolomites
    // world.anchor_x_tile = 273;
    // world.anchor_z_tile = 180;
    // rendering.skirt_drop = 1000.0f;


    // The Grand Canyon
    world.anchor_x_tile = 97;
    world.anchor_z_tile = 200;
    rendering.skirt_drop = 1000.0f;

    // Norway
    world.anchor_x_tile = 265;
    world.anchor_z_tile = 143;
    rendering.skirt_drop = 1000.0f;

    // Norway
    world.anchor_x_tile = 98;
    world.anchor_z_tile = 186;
    rendering.skirt_drop = 1000.0f;

    // crete
    // world.anchor_x_tile = 292;
    // world.anchor_z_tile = 202;
    // world.skirt_overlap = {
    //     {9, 1.02f},
    //     {10, 1.02f},
    //     {11, 1.02f},
    //     {12, 1.02f},
    //     {13, 1.02f},
    //     {14, 1.02f},
    //     {15, 1.02f}
    // };
    // rendering.skirt_drop = 1000.0f;
    // rendering.height_scale = 1.5f;
    // rendering.fog_start = 40000.0f;


    raytiles::streamer streamer(world, streaming, rendering, pool_conf);
    raytiles::renderer &r = streamer.get_renderer();
    r.set_normals_scale(5.0f);

    Camera3D camera;
    camera.position = Vector3{2000.0f, 5000.0f, 2000.0f};
    camera.target = Vector3{3000.0f, 4750.0f, 3000.0f};
    camera.up = Vector3{0.0f, 1.0f, 0.0f};
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    FreeCamera f(camera);

    r.set_fog_color(SKYBLUE);
    r.set_ambient_light(Color{200, 200, 200, 255});
    float sun = 1.0f;
    bool wireframe = true;
    bool labels = true;

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


    while (!WindowShouldClose()) {
        const auto dt = GetFrameTime();
        streamer.update(camera);

        // if (streamer.is_loading()) {
        //     const auto loading = streamer.get_loading();
        //     BeginDrawing();
        //     ClearBackground(BLACK);
        //     DrawText(TextFormat("Loading... %.1f%%", loading * 100.0f), 10, 30, 20, WHITE);
        //     EndDrawing();
        //     continue;
        // }


        f.update(camera, dt);
        r.set_sun_direction(Vector3{0.1f, sun, 0.0f});

        BeginDrawing();
        ClearBackground(SKYBLUE);

        BeginMode3D(camera);
        // draw the world around the camera
        streamer.draw(camera);
        if (wireframe) streamer.debug_3d();
        EndMode3D();

        if (labels) {
            DrawRectangle(5, 5, 400, 100, Fade(BLACK, 0.5f));
            streamer.debug(camera);
        }

        DrawRectangle(5, 550, 600, 40, Fade(BLACK, 0.5f));
        DrawText("Controls: K to toggle labels, L to toggle wireframe", 10, 560, 20, WHITE);
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
