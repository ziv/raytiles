#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "raii.hpp"
#include "raylib.h"
#include "tile.hpp"

namespace raytiles {
/// One tile fetch: the anchor-relative identity plus the absolute provider
/// coordinates at `key.zoom` (the caller resolves the anchor; the source
/// knows nothing about world anchoring).
struct tile_request {
  tile_key key;
  int x;
  int z;
};

/// A completed tile: all three assets decoded to CPU images, plus the
/// compact height grid derived on the worker (main-thread upload budget is
/// the scarce resource; worker time is free parallelism). Move-only;
/// images are freed by raii on whatever thread destroys the payload — the
/// source guarantees that is always the main thread (drain or destructor).
struct tile_payload {
  tile_key key;
  raii::image albedo;
  raii::image height;
  raii::image normals;
  height_grid heights;
};

/// Background tile fetcher. One job per tile fetches texture + heightmap +
/// normals sequentially (disk cache or HTTP with atomic write-through),
/// decodes with stb_image, and delivers the finished payload through a
/// ready queue drained once per frame by the main thread.
///
/// Threading rules (load-bearing):
///   - workers never call any raylib function — not even UnloadImage;
///     failure-path pixel buffers are freed via stbi_image_free directly
///   - the condition_variable_any + jthread stop-token wait is required for
///     shutdown; a plain condition_variable would hang the destructor
///   - httplib stays confined to tile_source.cpp
class tile_source {
 public:
  /// A tile that will not arrive: fetch failed (`reason` set) or the job
  /// was cancelled (`cancelled == true`, empty reason). Delivered through
  /// drain() so the caller can clear its loading bookkeeping.
  struct drop {
    tile_key key;
    bool cancelled;
    std::string reason;
  };

  /// Splits each provider URL into host + path and derives the cache
  /// path templates from `net.cache_dir`. Throws `std::runtime_error`
  /// for URLs without a scheme.
  explicit tile_source(const network_config& net);

  /// Blocks until workers exit; an in-flight HTTP fetch cannot be
  /// aborted, so this may wait out a network timeout.
  ~tile_source();

  tile_source(const tile_source&) = delete;

  tile_source& operator=(const tile_source&) = delete;

  /// Queues one tile fetch. Dedup by key: a no-op while the same key is
  /// pending or executing (a cancelled-but-not-yet-collected job counts
  /// as in flight; its drop must be drained before the key can be
  /// requested again).
  void request(const tile_request& req);

  /// Flags the job for cancellation (checked at pickup and between
  /// assets). The key is always answered — either by a payload that was
  /// already past the last check, or by a cancelled drop.
  void cancel(const tile_key& key);

  /// Collects everything finished since the last drain: clears both out
  /// params and swaps the internal queues into them (one lock, zero
  /// allocations in steady state when the caller reuses its vectors).
  void drain(std::vector<tile_payload>& ready_out, std::vector<drop>& dropped_out);

 private:
  struct job {
    tile_request req;
    std::shared_ptr<std::atomic_bool> cancelled;
  };

  /// Low-priority synthesis work: along the ancestry of one derived tile,
  /// generate the missing sibling heightmaps at every level (4 children per
  /// lineage node — 28 PNGs at z22, never the full subtree, which would be
  /// ~21k tiles per native parent at deep zooms). Cousins outside the
  /// lineage derive on demand (~ms) and enqueue their own backfill.
  /// Enqueued after a synchronous derivation served its requested tile;
  /// processed only when no real job is pending. Best-effort (no
  /// cancellation, dropped at shutdown).
  struct derive_task {
    int zoom;  // the requested tile, absolute provider coords
    int x;
    int z;
  };

  /// network_config resolved for fetching: URLs split into host + path,
  /// cache templates derived from cache_dir.
  struct resolved {
    int threads;
    bool allow_insecure_tls;
    int connection_timeout_sec;
    int read_timeout_sec;
    int native_terrain_zoom;

    std::string texture_cache_path;
    std::string heightmap_cache_path;
    std::string normals_cache_path;

    std::string texture_host;
    std::string texture_url_path;

    std::string heightmap_host;
    std::string heightmap_url_path;

    std::string normals_host;
    std::string normals_url_path;
  };

  [[nodiscard]] static resolved resolve(const network_config& net);

  void worker_loop(const std::stop_token& st);

  /// Erase from in_flight and record the drop, under the lock.
  void deliver_drop(const tile_key& key, bool cancelled, std::string reason);

  /// Enqueue a derive task unless an identical one already ran (dedup).
  void enqueue_derive(int zoom, int x, int z);

  /// Execute one derive task (worker thread, no lock held).
  void run_derive(const derive_task& task, const std::stop_token& st);

  resolved options;
  std::vector<std::jthread> workers;

  std::queue<job> pending;
  std::unordered_map<tile_key, std::shared_ptr<std::atomic_bool> > in_flight;
  std::vector<tile_payload> ready;
  std::vector<drop> dropped;

  // low-priority synthesis work (see derive_task); background_done holds the
  // requested tiles whose lineage backfill already ran (dedup)
  std::queue<derive_task> background;
  std::unordered_set<tile_key> background_done;

  std::mutex mtx;
  std::condition_variable_any cv;
};
}  // namespace raytiles
