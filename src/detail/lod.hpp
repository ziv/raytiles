#pragma once
/// Pure LOD policy: computes the desired tile set for a camera position.
///
/// This header must stay pure — no raylib calls, no I/O, no globals — the
/// unit tests in tests/lod_tests.cpp (including exact desired-set snapshots)
/// depend on `desired_tiles` being a deterministic function of
/// (options, position). raylib *types* (Vector3) are fine; raylib *functions*
/// are not.
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "raytiles/raytiles.h"
#include "tile.hpp"
#include "utils.hpp"

namespace raytiles::lod {
/// The subset of the streamer configuration the desired-set policy needs.
/// Thresholds are plain meters (as in the public `streaming_config`);
/// derived values (per-zoom sizes, squared thresholds, horizon radius)
/// are computed inside `desired_tiles` so the policy stays stateless.
struct options {
  int base_zoom = min_supported_zoom;
  int max_zoom = 15;  // matches world_config::max_zoom default

  /// World size (meters) of one tile at `base_zoom`.
  float base_tile_size = 66400.0f;

  /// Radius, in base-zoom tiles, of the disc scanned around the camera.
  int rendering_radius = 6;

  /// Per-zoom subdivision thresholds, indexed `thresholds[zoom - base_zoom]`.
  std::array<Meters, zoom_levels> thresholds = {100000.0f, 80000.0f, 40000.0f, 20000.0f, 10000.0f, 5000.0f, 2500.0f,
                                                1250.0f,   625.0f,   312.0f,   156.0f,   78.0f,    39.0f,   20.0f};
};

namespace detail {
struct zoom_meta {
  float size;
  MetersDSq threshold_sq;
};

struct build_ctx {
  int max_zoom;
  int base_zoom;
  const std::array<zoom_meta, zoom_levels>& metas;
  const Vector3& position;
  float render_radius_sq;
  std::vector<tile_key>& out;
};

/// Recursive subdivision: accept a tile when it is far enough for its
/// zoom (or already at max_zoom), reject it beyond the horizon,
/// subdivide otherwise. Order of checks is load-bearing and mirrored
/// by the reference implementation in tests/lod_tests.cpp.
inline void build_required(const build_ctx& ctx, const Zoom zoom, const int tx, const int tz) {
  if (zoom == ctx.max_zoom) {
    ctx.out.push_back({zoom, tx, tz});
    return;
  }

  const auto& meta = ctx.metas[static_cast<std::size_t>(zoom - ctx.base_zoom)];

  // calculate distance of the tile from the camera
  const MetersDSq distance_sq = utils::distance_sq_to_tile(ctx.position, {zoom, tx, tz}, meta.size);

  // not in the area we render at all
  if (distance_sq > ctx.render_radius_sq) {
    return;
  }

  // do we need to subdivide?
  if (distance_sq >= meta.threshold_sq) {
    ctx.out.push_back({zoom, tx, tz});
    return;
  }

  const int child_zoom = zoom + 1;
  const int cx0 = tx * 2;
  const int cz0 = tz * 2;
  for (int ox = 0; ox < 2; ++ox)
    for (int oz = 0; oz < 2; ++oz) build_required(ctx, child_zoom, cx0 + ox, cz0 + oz);
}
}  // namespace detail

/// Appends the desired tile keys for `position` (absolute space) into `out`.
/// Does not clear `out`; the produced keys are duplicate-free. Callers reuse
/// the vector across invocations so steady-state runs allocation-free.
inline void desired_tiles(const options& opts, const Vector3& position, std::vector<tile_key>& out) {
  // per-zoom size / squared threshold, derived exactly as the streamer
  // constructor derives them (float math, then widened) so behavior is
  // identical to the pre-extraction implementation
  std::array<detail::zoom_meta, zoom_levels> metas{};
  for (int zoom = opts.base_zoom; zoom <= opts.max_zoom; ++zoom) {
    const auto idx = static_cast<std::size_t>(zoom - opts.base_zoom);
    const auto ratio = static_cast<float>(1 << (zoom - opts.base_zoom));
    const auto th = opts.thresholds[idx];
    metas[idx] = detail::zoom_meta{opts.base_tile_size / ratio, th * th};
  }

  const int current_tile_x = static_cast<int>(std::floor(position.x / opts.base_tile_size));
  const int current_tile_z = static_cast<int>(std::floor(position.z / opts.base_tile_size));

  const auto r = opts.rendering_radius;
  const auto allowed_radius = (r - 1) * (r - 1);

  // rendering limit radius based on the horizon distance from the current
  // camera height: tiles beyond that point won't be visible anyway, so no
  // need to even request them
  const auto render_radius_sq = static_cast<float>(utils::calculate_horizon(position));

  const detail::build_ctx ctx{opts.max_zoom, opts.base_zoom, metas, position, render_radius_sq, out};

  for (int dx = -r; dx <= r; ++dx)
    for (int dz = -r; dz <= r; ++dz)
      if (dz * dz + dx * dx < allowed_radius) detail::build_required(ctx, opts.base_zoom, current_tile_x + dx, current_tile_z + dz);
}
}  // namespace raytiles::lod
