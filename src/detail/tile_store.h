#pragma once
/// @file tile_store.h
/// The resident tile set: everything with a lifetime. Owns the per-zoom
/// meshes, the GPU textures + CPU heightmaps of promoted tiles, and the
/// bookkeeping that decides what stays resident (eviction/coverage) and what
/// gets requested next (via the `lod` policy).
///
/// It replaces the old `tiles_manager` three-phase pre/process/post protocol
/// with named frame steps the streamer calls in a fixed order:
///
///   reconcile(pos, src)      every frame   evict stale tiles, cancel
///                                          downloads that fell out of the
///                                          desired set
///   promote(src)             every frame   drain finished payloads and
///                                          upload to GPU under the frame
///                                          budget
///   update_desired(pos, src) on movement   recompute the desired set (pure
///                                          lod policy) and request what's
///                                          missing
///   cull(frustum, offset)    every frame   set per-tile visibility for the
///                                          renderer
///
/// Coordinate convention: positions here are *absolute* world space
/// (`absolute = user - world_offset`); only `cull` sees the user-space
/// frustum and bridges the two via `world_offset`.

#include <array>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "lod.hpp"
#include "raylib.h"
#include "raytiles/raytiles.h"
#include "tile.hpp"
#include "tile_source.h"

namespace raytiles {
/// Construction inputs for `tile_store`, translated from the public
/// config structs by the streamer. Validation (zoom bounds) happens in
/// the constructor, before any GL resource is created.
struct store_options {
  int base_zoom = min_supported_zoom;
  int max_zoom = max_supported_zoom;

  /// World size (meters) of one tile at `base_zoom`; higher zooms halve it.
  float base_zoom_tile_size = 66400.0f;

  /// Anchor tile at `base_zoom`: translates anchor-relative tile keys
  /// to the provider's absolute slippy coordinates.
  int anchor_x_tile = 306;
  int anchor_z_tile = 207;

  /// Disc radius (in base-zoom tiles) handed to the lod policy.
  int rendering_radius = 6;

  /// Generate trilinear/anisotropic mipmaps for albedo textures on upload.
  bool use_mipmap = true;

  /// Per-frame wall-clock budget (seconds) for GPU uploads in `promote`.
  double upload_budget_sec = 0.002;

  /// Hard cap on tile promotions per frame, on top of the budget.
  int max_uploads_per_frame = 8;

  /// Per-zoom subdivision distances (meters), indexed `[zoom - base_zoom]`.
  std::array<Meters, zoom_levels> thresholds = {100000.0f, 80000.0f, 40000.0f, 20000.0f, 10000.0f, 5000.0f, 2500.0f};

  /// Per-zoom skirt overlap factors baked into the generated meshes.
  std::array<float, zoom_levels> skirt_overlap = {1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f};
};

class tile_store {
 public:
  /// Builds the per-zoom plane meshes (requires a live GL context) and
  /// validates zoom bounds — throws `std::runtime_error` on violation.
  explicit tile_store(const store_options& opts);

  /// Every frame, first step: evicts resident tiles that are no longer
  /// wanted (not desired, out of frustum, beyond the horizon, or
  /// covered by other zoom levels) and cancels in-flight downloads
  /// that fell out of the desired set.
  void reconcile(const Vector3& abs_position, tile_source& source);

  /// Every frame, second step: drains completed payloads from the
  /// source and promotes them to GPU textures. Uploads are bounded by
  /// `upload_budget_sec` and `max_uploads_per_frame`; leftovers wait in
  /// an internal pending buffer for the next frame. Failed keys are
  /// forgotten (a later desired-set rebuild may re-request them).
  void promote(tile_source& source);

  /// On camera movement: recomputes the desired tile set (pure `lod`
  /// policy) and requests every tile that is neither resident nor
  /// already loading.
  void update_desired(const Vector3& abs_position, tile_source& source);

  /// Every frame, last step: updates each resident tile's
  /// `in_frustum_this_frame` flag. `world_offset` maps absolute tile
  /// coords into the frustum's user-space frame (`user = abs + offset`).
  void cull(const Frustum& frustum, const Vector3& world_offset);

  /// Terrain altitude under an absolute-space position, sampled from
  /// the finest resident tile covering it. O(1) pixel read from the
  /// CPU-retained heightmap.
  [[nodiscard]] std::optional<float> ground_height(const Vector3& abs_position) const;

  /// True until the initial load completes (first moment nothing is
  /// loading or pending upload).
  [[nodiscard]] bool loading() const { return loading_; }

  /// Initial-load progress in [0, 1], monotonically non-decreasing
  /// (unlike the old `get_loading`, it cannot run backwards when the
  /// desired set changes mid-load). Returns 1 once loading is done.
  [[nodiscard]] float progress() const;

  /// Resident tiles, keyed anchor-relative. Each `loaded_tile` carries
  /// its mesh pointer and visibility flag, so the renderer iterates
  /// this map with zero further lookups.
  [[nodiscard]] const std::unordered_map<tile_key, loaded_tile>& tiles() const { return rendering_tiles; }

  /// Currently desired keys (debug overlays only).
  [[nodiscard]] const std::unordered_set<tile_key>& desired() const { return desired_keys; }

 private:
  /// Per-zoom immutable data, indexed `[zoom - base_zoom]` (flat array:
  /// these are read in the ground_height / draw hot paths).
  struct zoom_entry {
    float size = 0.0f;
    raii::mesh mesh;
  };

  [[nodiscard]] const zoom_entry& entry(const Zoom zoom) const { return zooms_[static_cast<std::size_t>(zoom - options.base_zoom)]; }

  /// Queues a download for `key`, translating anchor-relative to
  /// absolute provider coordinates, and tracks it as loading.
  void request(const tile_key& key, tile_source& source);

  [[nodiscard]] bool is_tile_out_of_area(const tile_key& key, const Vector3& position) const;

  /// True when the area of `key` is fully covered by resident tiles of
  /// nearby zoom levels (parent, children, grandparent, grandchildren),
  /// i.e. evicting it cannot open a hole in the terrain.
  [[nodiscard]] bool is_tile_covered(const tile_key& key) const;

  store_options options;
  lod::options lod_options;

  std::array<zoom_entry, zoom_levels> zooms_{};

  // what should be resident for the current camera position; refreshed
  // by update_desired
  std::unordered_set<tile_key> desired_keys;

  // requested from the source but not yet promoted (downloading, or
  // sitting in pending_uploads)
  std::unordered_set<tile_key> loading_keys;

  // completed payloads that missed a previous frame's upload budget
  std::vector<tile_payload> pending_uploads;

  // resident tiles: GPU textures + CPU heightmap
  std::unordered_map<tile_key, loaded_tile> rendering_tiles;

  bool loading_ = true;
  float progress_ = 0.0f;  // high-water mark, see progress()
};
}  // namespace raytiles
