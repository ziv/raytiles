#include "raytiles/rayskies.h"

#include "detail/sky_renderer.h"

namespace raytiles::sky {
sky_steamer::sky_steamer(const sky_config& config) : renderer(std::make_unique<sky_renderer>(config)) {}

sky_steamer::~sky_steamer() = default;

void sky_steamer::draw(const Vector3 player_pos) const { renderer->draw(player_pos); }

void sky_steamer::set_horizon_color(const float r, const float g, const float b) const { renderer->get_shader().set_horizon_color(r, g, b); }

void sky_steamer::set_horizon_color(const Color color) const {
  const auto r = static_cast<float>(color.r) / 255.0f;
  const auto g = static_cast<float>(color.g) / 255.0f;
  const auto b = static_cast<float>(color.b) / 255.0f;
  renderer->get_shader().set_horizon_color(r, g, b);
}

void sky_steamer::set_zenith_color(float r, float g, float b) const { renderer->get_shader().set_zenith_color(r, g, b); }

void sky_steamer::set_zenith_color(const Color color) const {
  const auto r = static_cast<float>(color.r) / 255.0f;
  const auto g = static_cast<float>(color.g) / 255.0f;
  const auto b = static_cast<float>(color.b) / 255.0f;
  renderer->get_shader().set_zenith_color(r, g, b);
}
}  // namespace raytiles::sky
