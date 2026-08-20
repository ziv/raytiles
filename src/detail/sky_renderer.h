#pragma once
#include "raytiles/rayskies.h"
#include "sky_shader.h"

namespace raytiles::sky {
class sky_renderer {
  sky_shader shader;
  raii::model sky_model;

 public:
  explicit sky_renderer(const sky_config& config);

  void draw(Vector3 player_pos);

  sky_shader& get_shader() { return shader; }
};
}  // namespace raytiles::sky
