#pragma once
#include "raii.hpp"
#include "raytiles/raytiles.h"

namespace raytiles {
/// Owns the terrain displacement shader and its uniform state. Configured
/// directly from the public `rendering_config` — no mirror struct.
class tile_shader {
 public:
  explicit tile_shader(const rendering_config& conf = {});

  /// Returns the underlying raylib `Shader` handle. Use this when you
  /// need to bind the shader to a `Material` or pass it to a raylib
  /// drawing call directly.
  const Shader& operator()() const noexcept { return *shader; }

  /// Pushes the current camera world-space position to the shader. Call
  /// this once per frame before drawing; the displacement shader uses it
  /// for fog falloff and per-vertex distance attenuation.
  tile_shader& set_camera_location(const Vector3& position);

  /// Re-uploads every uniform from `conf` in one call (bulk setter).
  tile_shader& apply(const rendering_config& conf);

  /// Sets the fog color used for distance attenuation. Match this to
  /// your sky color for a seamless horizon.
  tile_shader& set_fog_color(Color color);

  /// Sets the fog start distance (meters) — the distance from the camera
  /// at which colors begin to blend with the fog.
  tile_shader& set_fog_start(float distance);

  /// Sets the fog end distance (meters) — the distance from the camera
  /// at which colors are fully replaced by the fog color.
  tile_shader& set_fog_end(float distance);

  /// Sets the vertical drop (meters) of the skirt geometry below each
  /// tile's edge. Larger values hide cracks between LODs more reliably
  /// but cost more fill rate. Set to `0` to disable.
  tile_shader& set_skirt_drop(float drop);

  /// Sets the world ambient light color. Use this to drive day / night /
  /// weather lighting changes.
  tile_shader& set_ambient_light(Color color);

  /// Sets the sun direction vector used by the shader's lighting pass.
  /// Magnitude is irrelevant — the shader normalizes it internally.
  tile_shader& set_sun_direction(Vector3 direction);

  /// Sets the sun lighting intensity. Controls the contrast between lit
  /// and shaded areas of the terrain.
  tile_shader& set_sun_scale(float scale);

  /// Sets the heightmap scale factor, exaggerating or flattening the
  /// terrain relief (drama factor). `1.0` keeps real-world elevation.
  tile_shader& set_height_scale(float scale);

  /// Sets the normals scale factor to increase or reduce lighting
  /// contrast. Higher values look bumpier but can produce lighting
  /// artifacts if the normals become too steep.
  tile_shader& set_normals_scale(float scale);

 private:
  /// Uploads every uniform of `options` to the GPU.
  void upload_all();

  // current values, mirrored to the GPU on each setter call
  rendering_config options;

  // shaders slots locations
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

  // shader
  raii::shader shader;
};
}  // namespace raytiles
