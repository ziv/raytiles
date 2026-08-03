#include "detail/tile_store.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <ranges>
#include <stdexcept>
#include <utility>

#include "detail/utils.hpp"

namespace raytiles {
tile_store::tile_store(const store_options& opts)
    : options(opts),
      lod_options{
          .base_zoom = opts.base_zoom,
          .max_zoom = opts.max_zoom,
          .base_tile_size = opts.base_zoom_tile_size,
          .radius = opts.rendering_radius,
          .thresholds = opts.thresholds,
      } {
  // input validation — before any GL resource is created
  if (options.base_zoom < min_supported_zoom) {
    throw std::runtime_error(std::format("base_zoom {} is below min_supported_zoom {}", options.base_zoom, min_supported_zoom));
  }
  if (options.max_zoom > max_supported_zoom) {
    throw std::runtime_error(std::format("max_zoom {} is above max_supported_zoom {}", options.max_zoom, max_supported_zoom));
  }
  if (options.max_zoom < options.base_zoom) {
    throw std::runtime_error(std::format("max_zoom {} is below base_zoom {}", options.max_zoom, options.base_zoom));
  }

  // one plane mesh per zoom level; resolution doubles with zoom (finer
  // displacement close to the camera) up to max_resolution
  int res = min_resolution;
  for (int zoom = options.base_zoom; zoom <= options.max_zoom; ++zoom) {
    const auto idx = static_cast<std::size_t>(zoom - options.base_zoom);
    const auto ratio = static_cast<float>(1 << (zoom - options.base_zoom));
    const auto size = options.base_zoom_tile_size / ratio;
    const auto skirt_factor = options.skirt_overlap[idx];

    zooms_[idx].size = size;
    zooms_[idx].mesh = raii::mesh{GenMeshPlane(size * skirt_factor, size * skirt_factor, res, res)};
    res = std::min(res * 2, max_resolution);
  }
}

float tile_store::progress() const {
  if (!loading_) return 1.0f;
  return progress_;
}

std::optional<float> tile_store::ground_height(const Vector3& abs_position) const {
  // walk from the highest available zoom down to base; whichever zoom holds the
  // tile that contains (position.x, position.z) wins. higher zoom = finer
  // sample, so we prefer it if loaded.
  for (int zoom = options.max_zoom; zoom >= options.base_zoom; --zoom) {
    const float size = entry(zoom).size;

    const int tile_x = static_cast<int>(std::floor(abs_position.x / size));
    const int tile_z = static_cast<int>(std::floor(abs_position.z / size));

    const auto it = rendering_tiles.find(tile_key{zoom, tile_x, tile_z});
    if (it == rendering_tiles.end()) continue;

    const Image& img = *it->second.hm_image;

    // local uv inside the tile, [0, 1)
    const float u = (abs_position.x - static_cast<float>(tile_x) * size) / size;
    const float v = (abs_position.z - static_cast<float>(tile_z) * size) / size;

    const int px = static_cast<int>(u * static_cast<float>(img.width));
    const int py = static_cast<int>(v * static_cast<float>(img.height));

    return utils::get_height_from_image(img, px, py);
  }
  return std::nullopt;
}

void tile_store::reconcile(const Vector3& abs_position, tile_source& source) {
  // gc: evict resident tiles nobody needs anymore
  std::erase_if(rendering_tiles, [&](const auto& item) {
    // if it in desired, keep it
    if (desired_keys.contains(item.first)) return false;

    // if it is base zoom and not desired, no need to
    // check the rest, remove it. it the horizon.
    if (item.first.zoom == options.base_zoom) return true;

    // if not in desired and not in frustum, remove
    // without thinking todo add softer eviction
    if (!item.second.in_frustum_this_frame) return true;

    // if the tile is far beyond the horizon, remove
    // without thinking
    if (is_tile_out_of_area(item.first, abs_position)) return true;

    // here we stop to think (this is the "slow" path)
    // if the tile is not covered by other tiles, keep
    // it to avoid holes in the surface
    if (!is_tile_covered(item.first)) return false;
    return true;
  });

  // stop loading tiles that fell out of the desired set. cancellation
  // is best-effort: a payload that slips through anyway is dropped by
  // the desired-set check in promote().
  for (auto it = loading_keys.begin(); it != loading_keys.end();) {
    if (!desired_keys.contains(*it)) {
      source.cancel(*it);
      it = loading_keys.erase(it);
    } else {
      ++it;
    }
  }
}

void tile_store::promote(tile_source& source) {
  // a failed tile (network / decode error, already logged by the
  // worker) is simply forgotten; a later desired-set rebuild may
  // request it again.
  for (const auto& key : source.drain_failures()) loading_keys.erase(key);

  // collect payloads completed since last frame. moves only — the
  // decoded pixel buffers keep their single owner all the way from the
  // worker to the GPU upload below.
  for (auto&& payload : source.drain()) pending_uploads.push_back(std::move(payload));

  // promote pending payloads into rendering_tiles. uploads are bounded
  // both by a wall-clock budget (to keep the frame steady on slow GPUs)
  // and a hard cap (to keep heavy single-tile uploads from running
  // away). PNG decode already happened off-thread, so this loop only
  // does GPU upload + bookkeeping.
  const double frame_start = GetTime();
  int promoted = 0;

  while (!pending_uploads.empty()) {
    tile_payload payload = std::move(pending_uploads.back());
    pending_uploads.pop_back();
    const tile_key key = payload.key;
    loading_keys.erase(key);

    // do we still need it? (dropping frees the CPU images here)
    if (!desired_keys.contains(key)) continue;

    // upload to GPU and move into rendering_tiles. the heightmap CPU image is
    // kept in the loaded_tile for ground_height() queries (recast, collision).
    raii::texture texture_tex = raii::load_texture_from_image(*payload.albedo);
    raii::texture height_tex = raii::load_texture_from_image(*payload.height);
    raii::texture normals_tex = raii::load_texture_from_image(*payload.normals);

    // don't clamp the ends
    SetTextureWrap(*texture_tex, TEXTURE_WRAP_CLAMP);
    SetTextureWrap(*height_tex, TEXTURE_WRAP_CLAMP);
    SetTextureWrap(*normals_tex, TEXTURE_WRAP_CLAMP);

    // allow use of mipmaps (only for texture)
    if (options.use_mipmap) {
      GenTextureMipmaps(&texture_tex.get());
      SetTextureFilter(*texture_tex, TEXTURE_FILTER_ANISOTROPIC_16X);
    }

    // world-space tile center, matching the mesh translation in draw
    const auto& ze = entry(key.zoom);
    const auto tx = (static_cast<double>(key.x) + 0.5) * static_cast<double>(ze.size);
    const auto tz = (static_cast<double>(key.z) + 0.5) * static_cast<double>(ze.size);

    rendering_tiles.insert_or_assign(key, loaded_tile{ze.size, tx, tz, &ze.mesh.get(), std::move(texture_tex), std::move(height_tex), std::move(payload.height),
                                                      std::move(normals_tex), false});

    ++promoted;

    if (promoted >= options.max_uploads_per_frame) break;
    if (GetTime() - frame_start >= options.upload_budget_sec) break;
  }

  // progress is a high-water mark: the desired set can grow mid-load,
  // but the reported fraction never runs backwards.
  if (!desired_keys.empty()) {
    const auto required = static_cast<float>(desired_keys.size());
    const auto outstanding = static_cast<float>(loading_keys.size());
    progress_ = std::max(progress_, std::clamp(1.0f - outstanding / required, 0.0f, 1.0f));
  }
}

void tile_store::update_desired(const Vector3& abs_position, tile_source& source) {
  // the desired-set policy is a pure function (see lod.hpp); this
  // method only owns the side effect of requesting missing tiles.
  lod::desired_tiles(lod_options, abs_position, desired_keys);

  for (const auto& key : desired_keys)
    if (!rendering_tiles.contains(key) && !loading_keys.contains(key)) request(key, source);
}

void tile_store::cull(const Frustum& frustum, const Vector3& world_offset) {
  for (auto& tile : rendering_tiles | std::views::values) {
    // Shift absolute tile center into user space before testing
    // against the user-space frustum.
    const auto user_x = static_cast<float>(tile.tx + static_cast<double>(world_offset.x));
    const auto user_z = static_cast<float>(tile.tz + static_cast<double>(world_offset.z));
    tile.in_frustum_this_frame = utils::is_tile_in_frustum(user_x, user_z, tile.size, frustum);
  }

  // first time nothing is loading or pending upload, initial load is done
  if (loading_ && loading_keys.empty()) {
    loading_ = false;
  }
}

void tile_store::request(const tile_key& key, tile_source& source) {
  // tile keys are anchor-relative; the provider wants absolute slippy
  // coordinates, so shift by the anchor scaled to this key's zoom.
  const auto scale = 1 << (key.zoom - options.base_zoom);
  source.request(key, key.x + options.anchor_x_tile * scale, key.z + options.anchor_z_tile * scale);
  loading_keys.insert(key);
}

bool tile_store::is_tile_out_of_area(const tile_key& key, const Vector3& position) const {
  const MetersDSq distance_sq = utils::distance_sq_to_tile_xz(position, key, entry(key.zoom).size);
  return distance_sq > utils::calculate_horizon(position);
}

bool tile_store::is_tile_covered(const tile_key& key) const {
  const auto contains = [&](const int zoom, const int x, const int z) { return rendering_tiles.contains(tile_key{zoom, x, z}); };

  // check parent
  if (key.zoom > options.base_zoom) {
    if (contains(key.zoom - 1, key.x >> 1, key.z >> 1)) return true;
  }

  // check children
  if (key.zoom < options.max_zoom) {
    const int child_x = key.x * 2;
    const int child_z = key.z * 2;
    if (const int target_zoom = key.zoom + 1; contains(target_zoom, child_x, child_z) && contains(target_zoom, child_x + 1, child_z) &&
                                              contains(target_zoom, child_x, child_z + 1) && contains(target_zoom, child_x + 1, child_z + 1)) {
      return true;
    }
  }

  // check grandparent. rare, but happens when zoom levels are skipped
  // due to distance-based loading in a very fast movement.
  if (key.zoom - 1 > options.base_zoom) {
    if (contains(key.zoom - 2, key.x >> 2, key.z >> 2)) return true;
  }

  // check grandchildren. rare, the same reason.
  if (key.zoom + 1 < options.max_zoom) {
    const int child_x = key.x * 4;
    const int child_z = key.z * 4;
    const int target_zoom = key.zoom + 2;
    for (int ox = 0; ox < 4; ++ox)
      for (int oz = 0; oz < 4; ++oz)
        if (!contains(target_zoom, child_x + ox, child_z + oz)) return false;
    return true;
  }

  return false;
}
}  // namespace raytiles
