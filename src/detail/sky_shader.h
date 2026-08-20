#pragma once
#include <raylib.h>

#include "detail/raii.hpp"
#include "raytiles/rayskies.h"

namespace raytiles::sky {
class sky_shader {
 public:
  explicit sky_shader(const sky_config& opts = {});

  /// Returns the underlying raylib `Shader` handle. Use this when you
  /// need to bind the shader to a `Material` or pass it to a raylib
  /// drawing call directly.
  const Shader& operator()() const noexcept { return *shader; }

  sky_shader& set_zenith_color(float r, float g, float b);

  sky_shader& set_horizon_color(float r, float g, float b);

  sky_shader& set_time(float time);

 private:
  sky_config options;

  int zenith_color_loc = -1;
  int horizon_color_loc = -1;
  int time_loc = -1;

  raii::shader shader;
};
}  // namespace raytiles::sky
