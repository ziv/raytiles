#include "detail/tile_store.h"

#include <algorithm>
#include <cassert>
#include <format>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "detail/utils.hpp"
#include "raytiles/raytiles.h"

namespace raytiles {
tile_store::tile_store(const config& conf) : world(conf.world), streaming(conf.streaming), source(conf.network) {
  // input validation
  if (world.base_zoom < min_supported_zoom) {
    throw std::runtime_error(std::format("base_zoom {} is below min_supported_zoom {}", world.base_zoom, min_supported_zoom));
  }
  if (world.max_zoom > max_supported_zoom) {
    throw std::runtime_error(std::format("max_zoom {} is above max_supported_zoom {}", world.max_zoom, max_supported_zoom));
  }
  if (world.max_zoom < world.base_zoom) {
    throw std::runtime_error(std::format("max_zoom {} is below base_zoom {}", world.max_zoom, world.base_zoom));
  }

  // desired-set policy inputs (pure lod module)
  lod_opts = lod::options{
      world.base_zoom, world.max_zoom, world.tile_size, streaming.radius, streaming.thresholds,
  };

  // construct the tiles map
  // for each zoom level:
  // - metadata (size & threshold)
  // - mesh
  int res = min_resolution;
  for (int zoom = world.base_zoom; zoom <= world.max_zoom; ++zoom) {
    const auto idx = static_cast<std::size_t>(zoom - world.base_zoom);
    const auto ratio = static_cast<float>(1 << (zoom - world.base_zoom));
    const auto size = world.tile_size / ratio;
    // squared in double (same pattern CodeQL flags in lod.hpp; kept in
    // lockstep even though this stored threshold is currently unread)
    const auto th = static_cast<MetersDSq>(streaming.thresholds[idx]);
    const auto skirt_factor = world.skirt_overlap[idx];

    tiles[idx] = tile_value{size, th * th, raii::mesh{GenMeshPlane(size * skirt_factor, size * skirt_factor, res, res)}};
    res = std::min(res * 2, max_resolution);
  }
}

bool tile_store::is_loading() const { return loading; }

float tile_store::get_loading() const {
  // fraction of the desired set that is resident. monotonic during the
  // initial load, 1.0 when everything desired is on the GPU.
  if (desired_keys.empty()) return 0.0f;
  std::size_t resident = 0;
  for (const auto& key : desired_keys)
    if (resident_tiles.contains(key)) ++resident;
  return static_cast<float>(resident) / static_cast<float>(desired_keys.size());
}

std::optional<float> tile_store::ground_height(const Vector3& position) const {
  // walk from the highest available zoom down to base; whichever zoom holds the
  // tile that contains (position.x, position.z) wins. higher zoom = finer
  // sample, so we prefer it if loaded.
  for (int zoom = world.max_zoom; zoom >= world.base_zoom; --zoom) {
    const float size = zoom_value(zoom).size;

    const int tile_x = static_cast<int>(std::floor(position.x / size));
    const int tile_z = static_cast<int>(std::floor(position.z / size));

    const auto it = resident_tiles.find(tile_key{zoom, tile_x, tile_z});
    if (it == resident_tiles.end()) continue;

    // defensive: promotion only stores populated grids, but a garbage
    // read here would be silent — keep the guard
    const auto& grid = it->second.heights;
    if (grid.samples.empty()) continue;

    // local uv inside the tile, [0, 1)
    const float u = (position.x - static_cast<float>(tile_x) * size) / size;
    const float v = (position.z - static_cast<float>(tile_z) * size) / size;

    return utils::sample_height_grid(grid, u, v);
  }
  return std::nullopt;
}

void tile_store::reconcile(const Vector3& position) {
  // gc
  std::erase_if(resident_tiles, [&](auto& entry) {
    auto& [key, tile] = entry;
    const bool remove = [&] {
      // if it in desired, keep it
      if (desired_keys.contains(key)) return false;

      // if it is base zoom and not desired, no need to
      // check the rest, remove it. it the horizon.
      if (key.zoom == world.base_zoom) return true;

      // if not in desired and not in frustum, remove
      // without thinking todo add softer eviction
      if (!render_list[tile.slot].visible) return true;

      // if the tile is far beyond the horizon, remove
      // without thinking
      if (is_tile_out_of_area(key, position)) return true;

      // here we stop to think (this is the "slow" path). coverage
      // can only have changed since the last pass if something was
      // promoted or the desired set was rebuilt — otherwise skip.
      if (!coverage_dirty) return false;

      // if the tile is not covered by other tiles, keep
      // it to avoid holes in the surface
      if (!is_tile_covered(key)) return false;
      return true;
    }();
    if (remove) evict(tile);
    return remove;
  });
  coverage_dirty = false;
}

void tile_store::evict(resident_tile& tile) {
  // swap-with-last removal from the flat list; the moved item's owner
  // record is re-pointed via the item's key backlink
  order_dirty = true;
  const auto slot = tile.slot;
  const auto last = static_cast<std::uint32_t>(render_list.size() - 1);
  if (slot != last) {
    render_list[slot] = render_list[last];
    resident_tiles.at(render_list[slot].key).slot = slot;
  }
  render_list.pop_back();
}

void tile_store::cull(const Frustum& frustum, const Vector3& world_offset) {
  // rebake transforms only after a large-world rebase. exact compare is
  // correct: the caller passes the same bits every frame until it rebases.
  if (world_offset.x != baked_offset.x || world_offset.z != baked_offset.z) {
    baked_offset = world_offset;
    const auto off_x = static_cast<double>(world_offset.x);
    const auto off_z = static_cast<double>(world_offset.z);
    for (auto& item : render_list) {
      // keep the addition in double so the huge-tile-coord + huge-offset
      // cancellation happens at full precision before the float cast
      item.transform = MatrixTranslate(static_cast<float>(item.abs_x + off_x), 0.0f, static_cast<float>(item.abs_z + off_z));
    }
  }

  for (auto& item : render_list) {
    item.visible = utils::is_tile_in_frustum(item.transform.m12, item.transform.m14, item.size, frustum);
  }
}

void tile_store::promote(const Vector3& position, const Vector3& world_offset) {
  // collect everything the source finished since last frame: one lock,
  // no polling. drops clear the loading bookkeeping (and log real
  // failures — cancellations are routine and stay quiet); payloads join
  // the upload queue.
  source.drain(ready_scratch, dropped_scratch);

  for (const auto& d : dropped_scratch) {
    loading_keys.erase(d.key);
    if (!d.cancelled) {
      // real failure: log it and let the next desired-set rebuild
      // retry — an immediate retry would hammer a failing server
      TraceLog(LOG_WARNING, "tile %d/%d/%d download failed: %s - dropping", d.key.zoom, d.key.x, d.key.z, d.reason.c_str());
    } else if (desired_keys.contains(d.key) && !resident_tiles.contains(d.key)) {
      // cancelled, but wanted again by the time the drop arrived
      // (camera came back) — request it right away
      spawn(d.key);
    }
  }

  upload_queue.insert(upload_queue.end(), std::make_move_iterator(ready_scratch.begin()), std::make_move_iterator(ready_scratch.end()));
  ready_scratch.clear();

  // budgeted GPU upload: bounded by a wall-clock budget (keeps the frame
  // steady on slow GPUs) and a hard cap (keeps heavy single-tile uploads
  // from running away). payloads that don't fit wait in the queue.
  const double frame_start = GetTime();
  int promoted = 0;

  while (!upload_queue.empty()) {
    tile_payload payload = std::move(upload_queue.back());
    upload_queue.pop_back();
    const tile_key key = payload.key;
    loading_keys.erase(key);

    // do we still need it? (raii frees the images if not)
    if (!desired_keys.contains(key)) continue;

    // upload to GPU. all three CPU images are freed at the end of this
    // iteration — the worker-built uint16 grid serves ground_height().
    raii::texture texture_tex = raii::load_texture_from_image(*payload.albedo);
    raii::texture height_tex = raii::load_texture_from_image(*payload.height);
    raii::texture normals_tex = raii::load_texture_from_image(*payload.normals);

    // don't clamp the ends
    SetTextureWrap(*texture_tex, TEXTURE_WRAP_CLAMP);
    SetTextureWrap(*height_tex, TEXTURE_WRAP_CLAMP);
    SetTextureWrap(*normals_tex, TEXTURE_WRAP_CLAMP);

    // allow use of mipmaps (only for texture)
    if (world.mipmaps) {
      GenTextureMipmaps(&texture_tex.get());
      SetTextureFilter(*texture_tex, TEXTURE_FILTER_ANISOTROPIC_16X);
    }

    // draw-ready entry for the flat render list. transform is baked in
    // user space; the double add preserves precision before the float cast.
    const auto& tv = zoom_value(key.zoom);
    const double abs_x = (static_cast<double>(key.x) + 0.5) * static_cast<double>(tv.size);
    const double abs_z = (static_cast<double>(key.z) + 0.5) * static_cast<double>(tv.size);
    const auto user_x = static_cast<float>(abs_x + static_cast<double>(world_offset.x));
    const auto user_z = static_cast<float>(abs_z + static_cast<double>(world_offset.z));
    const render_item item{
        *tv.mesh, *texture_tex, *height_tex, *normals_tex, MatrixTranslate(user_x, 0.0f, user_z), tv.size, abs_x, abs_z, key,
        false,  // visible: decided by cull later this frame
        true,   // desired: promotion only happens for still-desired keys
    };

    if (const auto existing = resident_tiles.find(key); existing != resident_tiles.end()) {
      // defensive: key already resident (shouldn't happen via the
      // spawn/promote flow) — replace resources in place, keep the slot
      render_list[existing->second.slot] = item;
      existing->second =
          resident_tile{std::move(texture_tex), std::move(height_tex), std::move(normals_tex), std::move(payload.heights), existing->second.slot};
    } else {
      render_list.push_back(item);
      const auto slot = static_cast<std::uint32_t>(render_list.size() - 1);
      resident_tiles.emplace(key, resident_tile{std::move(texture_tex), std::move(height_tex), std::move(normals_tex), std::move(payload.heights), slot});
      order_dirty = true;
      coverage_dirty = true;  // a new resident can cover its parent/children
    }

    ++promoted;

    if (promoted >= streaming.max_uploads_per_frame) break;
    if (GetTime() - frame_start >= streaming.upload_budget_sec) break;
  }

  // keep the list front-to-back for early-Z. only membership changes
  // (promote/evict) disturb the order, so steady-state frames skip this;
  // opaque rendering means order is a perf policy, never a visual one.
  if (order_dirty) {
    order_dirty = false;
    const auto dist_sq = [&](const render_item& item) {
      const double dx = item.abs_x - static_cast<double>(position.x);
      const double dz = item.abs_z - static_cast<double>(position.z);
      return dx * dx + dz * dz;
    };
    std::ranges::sort(render_list, [&](const render_item& a, const render_item& b) { return dist_sq(a) < dist_sq(b); });
    for (std::uint32_t i = 0; i < render_list.size(); ++i) resident_tiles.at(render_list[i].key).slot = i;
  }

#ifndef NDEBUG
  // render-list/owner lockstep invariant (swap-remove + sort bookkeeping)
  for (std::uint32_t i = 0; i < render_list.size(); ++i) assert(resident_tiles.at(render_list[i].key).slot == i);
#endif

  // initial loading is over once a desired set exists and nothing is in
  // flight or awaiting upload. the desired_keys guard is load-bearing:
  // promote runs BEFORE the first update_desired in the frame order, so
  // without it the flag would flip on frame one, before anything was ever
  // requested — and the caller's loading screen would never show.
  if (loading && !desired_keys.empty() && loading_keys.empty() && upload_queue.empty()) {
    loading = false;
  }
}

void tile_store::update_desired(const Vector3& position) {
  // the policy is pure; membership indexing stays here
  desired_scratch.clear();
  lod::desired_tiles(lod_opts, position, desired_scratch);

  desired_keys.clear();
  desired_keys.insert(desired_scratch.begin(), desired_scratch.end());
  coverage_dirty = true;  // eviction candidates changed with the set

  // refresh the debug-overlay flag on resident items (rebuilds are rare)
  for (auto& item : render_list) item.desired = desired_keys.contains(item.key);

  // cancel once, here: the desired set only changes in this function, so
  // this is the single place a loading tile can fall out of it
  for (const auto& key : loading_keys)
    if (!desired_keys.contains(key)) source.cancel(key);

  // spawn new if not in rendering list
  for (const auto& key : desired_keys)
    if (!resident_tiles.contains(key) && !loading_keys.contains(key)) spawn(key);
}

void tile_store::spawn(const tile_key& tile) {
  const auto scale = 1 << (tile.zoom - world.base_zoom);
  source.request(tile_request{
      tile,
      tile.x + world.anchor_x_tile * scale,
      tile.z + world.anchor_z_tile * scale,
  });
  loading_keys.insert(tile);
}

bool tile_store::is_tile_out_of_area(const tile_key& key, const Vector3& position) const {
  const MetersDSq distance_sq = utils::distance_sq_to_tile_xz(position, key, zoom_value(key.zoom).size);
  return distance_sq > utils::calculate_horizon(position);
}

bool tile_store::is_tile_covered(const tile_key& key) const {
  const auto contains = [&](const int zoom, const int x, const int z) { return resident_tiles.contains(tile_key{zoom, x, z}); };

  // check parent
  if (key.zoom > world.base_zoom) {
    if (contains(key.zoom - 1, key.x >> 1, key.z >> 1)) return true;
  }

  // check children
  if (key.zoom < world.max_zoom) {
    const int child_x = key.x * 2;
    const int child_z = key.z * 2;
    if (const int target_zoom = key.zoom + 1; contains(target_zoom, child_x, child_z) && contains(target_zoom, child_x + 1, child_z) &&
                                              contains(target_zoom, child_x, child_z + 1) && contains(target_zoom, child_x + 1, child_z + 1)) {
      return true;
    }
  }

  // check grandparent. rare, but happens when zoom levels are skipped
  // due to distance-based loading in a very fast movement.
  if (key.zoom - 1 > world.base_zoom) {
    if (contains(key.zoom - 2, key.x >> 2, key.z >> 2)) return true;
  }

  // check grandchildren. rare, the same reason.
  if (key.zoom + 1 < world.max_zoom) {
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
