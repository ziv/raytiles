#pragma once
/// @file terrain_renderer.h
/// Draws the resident tile set: owns the displacement shader (GLSL baked into
/// the binary), the shared material the tile textures are bound through, and
/// the per-uniform setters the streamer forwards to.
///
/// This is the merge of the former `tiles_renderer` (a pure forwarding layer)
/// and `tile_shader`: one class, one place where a uniform value lives. The
/// shader state mirror is the public `rendering_config` itself — there is no
/// internal options mirror to keep in sync.
///
/// GL contract: constructing requires a live GL context (`InitWindow` first);
/// `draw`/`debug_3d` must run inside `BeginMode3D`/`EndMode3D`, `debug`
/// (label overlay) after `EndMode3D`.

#include "raii.hpp"
#include "raylib.h"
#include "raytiles/raytiles.h"  // rendering_config (held by value as the uniform mirror)

namespace raytiles {
class tile_store;

class terrain_renderer {
 public:
  /// Compiles the displacement shader, resolves and validates all
  /// uniform locations (throws `std::runtime_error` on a missing one),
  /// and uploads `conf` as the initial uniform values.
  explicit terrain_renderer(const rendering_config& conf);

  /// Draws every store tile whose visibility flag was set by the last
  /// `tile_store::cull`. `position` (user space) feeds the shader's
  /// fog/distance term; `world_offset` shifts absolute tile centers
  /// into user space. Returns the number of tiles drawn.
  int draw(const Vector3& position, const Vector3& world_offset, const tile_store& store);

  /// Draws a 2D zoom-label overlay above the tiles (green = desired,
  /// red = resident-but-stale). Call after `EndMode3D`.
  static void debug(const Camera3D& camera, const Vector3& world_offset, const tile_store& store);

  /// Draws 3D tile-bound wireframes. Call inside the same
  /// `BeginMode3D` / `EndMode3D` block as `draw`.
  static void debug_3d(const Vector3& world_offset, const tile_store& store);

  /// @name Uniform setters
  /// Each records the value in the config mirror and pushes it to the
  /// GPU immediately. Safe any time after construction.
  /// @{

  /// Sets the world ambient light color. Use this to drive
  /// day / night / weather lighting changes.
  void set_ambient_light(float r, float g, float b, float a);
  void set_ambient_light(Color color);

  /// Sets the fog color used for distance attenuation. Match this to
  /// your sky color for a seamless horizon.
  void set_fog_color(float r, float g, float b, float a);
  void set_fog_color(Color color);

  /// Sets the distance (meters) at which colors begin to blend with the fog.
  void set_fog_start(float distance);

  /// Sets the distance (meters) at which colors are fully replaced by the fog.
  void set_fog_end(float distance);

  /// Sets the sun direction vector; the shader normalizes it internally.
  void set_sun_direction(Vector3 direction);

  /// Sets the sun lighting intensity (contrast between lit and shaded areas).
  void set_sun_scale(float scale);

  /// Sets the heightmap scale factor, exaggerating or flattening the
  /// terrain relief (drama factor). `1.0` keeps real-world elevation.
  void set_height_scale(float scale);

  /// Sets the normals scale factor. Higher values look bumpier but can
  /// produce lighting artifacts if the normals become too steep.
  void set_normals_scale(float scale);

  /// @}

 private:
  /// Pushes the camera position uniform; called once per draw().
  void set_camera_location(const Vector3& position);

  // current uniform values — the public config struct doubles as the
  // CPU-side mirror, so there is no internal copy to keep in sync
  rendering_config options;

  // cached uniform locations, resolved once at construction
  int cam_pos_loc = -1;
  int ambient_loc = -1;
  int fog_color_loc = -1;
  int tex_albedo_loc = -1;
  int tex_height_loc = -1;
  int tex_normal_loc = -1;
  int sun_dir_loc = -1;
  int sun_scale_loc = -1;
  int height_scale_loc = -1;
  int normal_scale_loc = -1;
  int fog_start_loc = -1;
  int fog_end_loc = -1;
  int skirt_drop_loc = -1;

  raii::shader shader;
  raii::material material{};
};
}  // namespace raytiles
