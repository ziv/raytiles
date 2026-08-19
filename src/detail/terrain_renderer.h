#pragma once

#include <span>

#include "raii.hpp"
#include "raylib.h"
#include "tile.hpp"
#include "tile_shader.h"

namespace raytiles {
struct rendering_config;

class terrain_renderer {
 public:
  explicit terrain_renderer(const rendering_config& conf);

  /// Draws every visible entry of the flat render list. The items carry
  /// everything needed (mesh, textures, baked transform) — no further
  /// lookups happen here. Returns the number of tiles drawn.
  int draw(const Vector3& camera_position, std::span<const render_item> items);

  /// Draws a 2D HUD with zoom labels above the tiles (green = desired,
  /// red = resident but no longer desired).
  /// Call between `BeginDrawing` / `EndDrawing`, after `EndMode3D`.
  static void debug(const Camera3D& camera, std::span<const render_item> items);

  /// Draws 3D debug overlays (tile bounds). Call inside the same
  /// `BeginMode3D` / `EndMode3D` block as `draw`.
  static void debug_3d(std::span<const render_item> items);

  /// Direct access to the shader's parameter surface (apply / set_*).
  /// The streamer forwards its public setters here — no per-parameter
  /// relay methods in between.
  [[nodiscard]] tile_shader& shader() { return shader_; }

 private:
  tile_shader shader_;
  raii::material material{};
};
}  // namespace raytiles
