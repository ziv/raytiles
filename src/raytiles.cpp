#include "../include/raytiles/raytiles.h"

#if defined(_WIN32)
#define NOGDI
#define NOUSER
#endif
#include <cmath>
#include <memory>
#include <optional>
#include <utility>

#include "detail/terrain_renderer.h"
#include "detail/tile_store.h"
#include "detail/utils.hpp"
#include "raylib.h"
#include "raymath.h"

namespace raytiles {
namespace {
/// Derive anchor tile, tile size, and origin offset for a geographic
/// coordinate, overriding those fields of `conf.world`.
config with_geo_anchor(config conf, const double latitude, const double longitude) {
  // calculate tiles
  const double lat = latitude * DEG2RAD;
  const double n = std::pow(2.0, min_supported_zoom);
  const double x = (longitude + 180.0) / 360.0 * n;
  const double y = (1.0 - std::log(std::tan(lat) + 1.0 / std::cos(lat)) / PI) / 2.0 * n;
  conf.world.anchor_x_tile = static_cast<int>(std::floor(x));
  conf.world.anchor_z_tile = static_cast<int>(std::floor(y));

  // calculate tile size
  constexpr double equator_circumference_m = 40075016.686;
  const double tile_size = equator_circumference_m * std::cos(lat) / n;
  conf.world.tile_size = static_cast<float>(tile_size);

  // calculate the offset
  const auto offset_x = static_cast<float>((x - conf.world.anchor_x_tile) * tile_size);
  const auto offset_z = static_cast<float>((y - conf.world.anchor_z_tile) * tile_size);
  conf.world.origin_offset = {offset_x, 0.0f, offset_z};

  TraceLog(LOG_INFO, "raytiles anchor tile %d %d", conf.world.anchor_x_tile, conf.world.anchor_z_tile);
  return conf;
}
}  // namespace

struct streamer::impl {
  float near_plane;
  float far_plane;
  float update_distance_sq;
  Vector3 init_position;

  terrain_renderer renderer;
  tile_store store;

  int rendered = 0;

  // update every frame
  Vector3 last_position = {-9999.9f, -9999.9f, -9999.9f};
  Frustum last_frustum{};

  // Cached current-frame inputs from update(); read by draw() and
  // ground_height(). Convention: cached_camera.position is in user
  // space, cached_world_offset maps user → absolute via
  // absolute = user - offset.
  Camera3D cached_camera{};
  Vector3 cached_world_offset = {0.0f, 0.0f, 0.0f};

  explicit impl(const config& conf)
      : near_plane(static_cast<float>(conf.streaming.near_plane)),
        far_plane(static_cast<float>(conf.streaming.far_plane)),
        update_distance_sq(conf.streaming.update_distance * conf.streaming.update_distance),
        init_position(conf.world.origin_offset),
        renderer(conf.rendering),
        store(conf) {}
};

streamer::streamer(config conf) : impl_(std::make_unique<impl>(conf)) {}

streamer::streamer(const double latitude, const double longitude, config conf) : streamer(with_geo_anchor(std::move(conf), latitude, longitude)) {}

streamer::~streamer() = default;

streamer::streamer(streamer&&) noexcept = default;

streamer& streamer::operator=(streamer&&) noexcept = default;

bool streamer::is_loading() const { return impl_->store.is_loading(); }

float streamer::loading_progress() const { return impl_->store.get_loading(); }

Vector3 streamer::initial_position(const float altitude) const { return impl_->init_position + Vector3{0.0f, altitude, 0.0f}; }

std::optional<float> streamer::ground_height(const Vector3 position) const {
  return impl_->store.ground_height(Vector3Subtract(position, impl_->cached_world_offset));
}

void streamer::update(const Camera3D& camera, const Vector3 world_offset) {
  auto& m = *impl_;

  // Cache for draw() and ground_height(); they are forbidden to take
  // these as args (single source of truth = update()).
  m.cached_camera = camera;
  m.cached_world_offset = world_offset;

  // Convert camera position from user space to absolute world space.
  // Internal pipeline (tile_store) operates in absolute space because
  // tile coordinates are stored absolute.
  const Vector3 abs_position = Vector3Subtract(camera.position, world_offset);

  m.store.reconcile(abs_position);
  m.store.promote(abs_position, world_offset);

  if (Vector3DistanceSqr(abs_position, m.last_position) > m.update_distance_sq) {
    m.last_position = abs_position;
    m.store.update_desired(abs_position);
  }

  // Frustum is built from the user-space camera (small floats) and is
  // therefore in user space. cull shifts each tile to user space
  // (abs + offset, baked into the item transform) before the test.
  m.last_frustum = utils::extract_frustum(camera, m.near_plane, m.far_plane);

  m.store.cull(m.last_frustum, world_offset);
}

void streamer::draw() { impl_->rendered = impl_->renderer.draw(impl_->cached_camera.position, impl_->store.render_items()); }

void streamer::draw_debug_3d() { terrain_renderer::debug_3d(impl_->store.render_items()); }

void streamer::draw_debug_labels() { terrain_renderer::debug(impl_->cached_camera, impl_->store.render_items()); }

void streamer::set_rendering(const rendering_config& conf) { impl_->renderer.shader().apply(conf); }
void streamer::set_fog_color(const Color color) { impl_->renderer.shader().set_fog_color(color); }
void streamer::set_fog_start(const float distance) { impl_->renderer.shader().set_fog_start(distance); }
void streamer::set_fog_end(const float distance) { impl_->renderer.shader().set_fog_end(distance); }
void streamer::set_ambient_light(const Color color) { impl_->renderer.shader().set_ambient_light(color); }
void streamer::set_sun_direction(const Vector3 direction) { impl_->renderer.shader().set_sun_direction(direction); }
void streamer::set_sun_scale(const float scale) { impl_->renderer.shader().set_sun_scale(scale); }
void streamer::set_height_scale(const float scale) { impl_->renderer.shader().set_height_scale(scale); }
void streamer::set_normals_scale(const float scale) { impl_->renderer.shader().set_normals_scale(scale); }
}  // namespace raytiles
