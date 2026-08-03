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
#include "detail/tile_source.h"
#include "detail/tile_store.h"
#include "detail/utils.hpp"
#include "raylib.h"
#include "raymath.h"

namespace raytiles {
namespace {
// Computes the world_config anchor fields from a geographic
// coordinate: the containing tile at min zoom, the local tile size
// (shrinks with latitude), and the world-space offset of the exact
// lat/lon inside that tile (so the camera can start right over it).
config& anchor_config_at(config& cfg, const double latitude, const double longitude) {
  // calculate tiles
  const double lat = latitude * DEG2RAD;
  const double n = std::pow(2.0, min_supported_zoom);
  const double x = (longitude + 180.0) / 360.0 * n;
  const double y = (1.0 - std::log(std::tan(lat) + 1.0 / std::cos(lat)) / PI) / 2.0 * n;
  cfg.world.anchor_x_tile = static_cast<int>(std::floor(x));
  cfg.world.anchor_z_tile = static_cast<int>(std::floor(y));

  // calculate tile size
  constexpr double equator_circumference_m = 40075016.686;
  const double tile_size = equator_circumference_m * std::cos(lat) / n;
  cfg.world.base_zoom_tile_size = static_cast<float>(tile_size);

  // calculate the offset
  const auto offset_x = static_cast<float>((x - cfg.world.anchor_x_tile) * tile_size);
  const auto offset_z = static_cast<float>((y - cfg.world.anchor_z_tile) * tile_size);
  cfg.world.offset = {offset_x, 0.0f, offset_z};

  TraceLog(LOG_INFO, "raytiles: anchor tiles %d %d", cfg.world.anchor_x_tile, cfg.world.anchor_z_tile);
  return cfg;
}

std::pair<std::string, std::string> split_url(const std::string& url) {
  const auto scheme = url.find("://");
  if (scheme == std::string::npos) throw std::runtime_error("invalid url (no scheme): " + url);

  const auto path_pos = url.find('/', scheme + 3);
  if (path_pos == std::string::npos) return {url, "/"};

  return {url.substr(0, path_pos), url.substr(path_pos)};
}

// Translates the public config into the tile_store's option struct.
store_options make_store_options(const world_config& world, const streaming_config& streaming) {
  return store_options{
      .base_zoom = world.base_zoom,
      .max_zoom = world.max_zoom,
      .base_zoom_tile_size = world.base_zoom_tile_size,
      .anchor_x_tile = world.anchor_x_tile,
      .anchor_z_tile = world.anchor_z_tile,
      .rendering_radius = streaming.rendering_radius,
      .use_mipmap = world.use_mipmap,
      .upload_budget_sec = streaming.upload_budget_sec,
      .max_uploads_per_frame = streaming.max_uploads_per_frame,
      .thresholds = streaming.thresholds,
      .skirt_overlap = world.skirt_overlap,
  };
}

// Translates the public network config into the tile_source's option
// struct — mainly splitting the full URL templates into host + path
// once, so workers never re-parse them.
source_options make_source_options(const network_config& network) {
  auto [texture_host, texture_url_path] = split_url(network.texture_url);
  auto [heightmap_host, heightmap_url_path] = split_url(network.heightmap_url);
  auto [normals_host, normals_url_path] = split_url(network.normals_url);
  return source_options{
      .download_threads = network.download_threads,
      .allow_insecure_tls = network.allow_insecure_tls,
      .texture_cache_path = network.texture_cache_path,
      .heightmap_cache_path = network.heightmap_cache_path,
      .normals_cache_path = network.normals_cache_path,
      .texture_host = std::move(texture_host),
      .texture_url_path = std::move(texture_url_path),
      .heightmap_host = std::move(heightmap_host),
      .heightmap_url_path = std::move(heightmap_url_path),
      .normals_host = std::move(normals_host),
      .normals_url_path = std::move(normals_url_path),
  };
}
}  // namespace

streamer::streamer(config cfg)
    : near_plane(static_cast<float>(cfg.streaming.near_plane)),
      far_plane(static_cast<float>(cfg.streaming.far_plane)),
      update_distance_sq(cfg.streaming.update_distance * cfg.streaming.update_distance),
      init_position(cfg.world.offset),
      source_(std::make_unique<tile_source>(make_source_options(cfg.network))),
      store_(std::make_unique<tile_store>(make_store_options(cfg.world, cfg.streaming))),
      renderer_(std::make_unique<terrain_renderer>(cfg.rendering)) {}

streamer::streamer(const double latitude, const double longitude, config cfg) : streamer(anchor_config_at(cfg, latitude, longitude)) {}

streamer::~streamer() = default;

streamer::streamer(streamer&&) noexcept = default;

streamer& streamer::operator=(streamer&&) noexcept = default;

bool streamer::is_loading() const { return store_->loading(); }

float streamer::loading_progress() const { return store_->progress(); }

Vector3 streamer::get_initial_position(const float y) const { return init_position + Vector3{0.0f, y, 0.0f}; }

std::optional<float> streamer::ground_height(const Vector3 position) const { return store_->ground_height(Vector3Subtract(position, cached_world_offset_)); }

void streamer::update(const Camera3D& camera, const Vector3 world_offset) {
  // Cache for draw() and ground_height(); they are forbidden to take
  // these as args (single source of truth = update()).
  cached_camera_ = camera;
  cached_world_offset_ = world_offset;

  // Convert camera position from user space to absolute world space.
  // The internal pipeline (store/source) operates in absolute space
  // because tile coordinates (tile.tx, tile.tz) are stored absolute.
  const Vector3 abs_position = Vector3Subtract(camera.position, world_offset);

  store_->reconcile(abs_position, *source_);
  store_->promote(*source_);

  if (Vector3DistanceSqr(abs_position, last_position) > update_distance_sq) {
    last_position = abs_position;
    store_->update_desired(abs_position, *source_);
  }

  // Frustum is built from the user-space camera (small floats) and is
  // therefore in user space. cull shifts each tile to user space
  // (tile.tx + offset.x) before the in-frustum test.
  last_frustum = utils::extract_frustum(camera, near_plane, far_plane);
  store_->cull(last_frustum, world_offset);
}

void streamer::draw() { rendered = renderer_->draw(cached_camera_.position, cached_world_offset_, *store_); }

void streamer::draw_debug_3d() { terrain_renderer::debug_3d(cached_world_offset_, *store_); }

void streamer::draw_debug_labels() { terrain_renderer::debug(cached_camera_, cached_world_offset_, *store_); }

void streamer::set_ambient_light(const Color color) const { renderer_->set_ambient_light(color); }

void streamer::set_fog_color(const Color color) const { renderer_->set_fog_color(color); }

void streamer::set_fog(const Color color, const float start, const float end) const {
  renderer_->set_fog_color(color);
  renderer_->set_fog_start(start);
  renderer_->set_fog_end(end);
}

void streamer::set_sun(const Vector3 direction, const float intensity) const {
  renderer_->set_sun_direction(direction);
  renderer_->set_sun_scale(intensity);
}

void streamer::set_height_scale(const float scale) const { renderer_->set_height_scale(scale); }

void streamer::set_normals_scale(const float scale) const { renderer_->set_normals_scale(scale); }
}  // namespace raytiles
