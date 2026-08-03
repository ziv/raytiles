#include <raytiles/raytiles.h>
#include <rlgl.h>

#include "fly.h"

int main() {
  SetTraceLogLevel(LOG_INFO);
  InitWindow(800, 600, "raytiles");

  // streamer configuration, set the anchor tiles (currently around greece)
  raytiles::config cfg;
  cfg.world.anchor_x_tile = 294;
  cfg.world.anchor_z_tile = 199;

  // create the streamer with the configuration
  raytiles::streamer streamer(cfg);
  streamer.set_fog_color(SKYBLUE);

  Camera3D camera;
  camera.position = Vector3{3000.0f, 5000.0f, 3000.0f};
  camera.target = Vector3{10000.0f, 0.0f, 1000.0f};
  camera.up = Vector3{0.0f, 1.0f, 0.0f};
  camera.fovy = 60.0f;
  camera.projection = CAMERA_PERSPECTIVE;

  FreeCamera f(camera);

  // Make sure we'll see the horizon
  rlSetClipPlanes(cfg.streaming.near_plane, cfg.streaming.far_plane);

  // loading loop
  for (;;) {
    constexpr Vector3 world_offset = {0.0f, 0.0f, 0.0f};
    streamer.update(camera, world_offset);
    if (!streamer.is_loading()) break;

    const auto loading = streamer.loading_progress();
    BeginDrawing();
    ClearBackground(BLACK);
    DrawText(TextFormat("Loading... %.1f%%", loading * 100.0f), 350, 350, 50, WHITE);
    EndDrawing();
  }

  while (!WindowShouldClose()) {
    f.update(camera, GetFrameTime());
    streamer.update(camera);

    BeginDrawing();
    ClearBackground(SKYBLUE);

    BeginMode3D(camera);
    // draw the world around the camera
    streamer.draw();
    EndMode3D();
    EndDrawing();
  }

  CloseWindow();
  return 0;
}
