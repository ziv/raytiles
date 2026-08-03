#pragma once
/// @file tile_source.h
/// Asynchronous tile provider: "request tile K" in, complete `tile_payload`
/// out. One background job per tile fetches all three assets (albedo,
/// heightmap, normals) — from the on-disk cache when present, over HTTPS
/// otherwise — decodes them off-thread, and delivers the finished payload
/// through a ready queue the main thread drains once per frame.
///
/// Design notes (see plan.md §2.1):
///  - One job per `tile_key`, not per asset. A tile is only renderable when
///    all three assets exist, so there is nothing to gain from resolving them
///    separately — and it deletes the per-asset future/cancel machinery.
///  - Delivery is a queue, not futures: `drain()` is a single lock + vector
///    swap. The main thread never polls readiness and never blocks on I/O.
///  - Everything is keyed by `tile_key`. Requesting, deduping, and cancelling
///    are O(1) set operations; no path strings are built outside workers.
///  - Failures do not throw across threads. A failed tile is logged by the
///    worker and reported via `drain_failures()` so the caller can forget it
///    (no retry — a later desired-set rebuild may request it again).
///
/// Threading contract: `request` / `cancel` / `drain` / `drain_failures` are
/// main-thread calls (any single thread works; calls are internally locked).
/// Workers never call raylib functions — raylib's decoders and TraceLog are
/// not thread-safe. PNG decode uses stb_image directly (reentrant), and the
/// pixel buffers it mallocs are released via `UnloadImage` (plain RL_FREE).
/// This header must never be included from a public header (it is httplib's
/// firewall: httplib appears only inside tile_source.cpp).

#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "raytiles/raytiles.h"
#include "tile.hpp"

namespace raytiles {
    /// Construction inputs for `tile_source`. URL templates arrive pre-split
    /// into host + path (the split happens once at streamer construction);
    /// cache path templates use `{}` slots for zoom/x/z via `std::vformat`.
    struct source_options {
        int download_threads = 4;
        bool allow_insecure_tls = false;

        std::string texture_cache_path = ".cache/texture/{}/{}/{}.png";
        std::string heightmap_cache_path = ".cache/heightmap/{}/{}/{}.png";
        std::string normals_cache_path = ".cache/normals/{}/{}/{}.png";

        std::string texture_host{};
        std::string texture_url_path{};

        std::string heightmap_host{};
        std::string heightmap_url_path{};

        std::string normals_host{};
        std::string normals_url_path{};
    };

    class tile_source {
    public:
        explicit tile_source(source_options opts);

        /// Blocks until workers exit; a worker mid-download finishes (or
        /// times out) first, so destruction can take a few seconds offline.
        ~tile_source();

        tile_source(const tile_source &) = delete;
        tile_source &operator=(const tile_source &) = delete;

        /// Queues a download job for `key`. No-op when the key is already in
        /// flight. `abs_x` / `abs_z` are the *absolute* (provider) tile
        /// coordinates — `key.x/z` are anchor-relative and only identify the
        /// tile to the caller. Re-requesting a previously cancelled key
        /// un-cancels it.
        void request(const tile_key &key, int abs_x, int abs_z);

        /// Best-effort cancellation: a job not yet picked up is discarded at
        /// pickup; a job mid-fetch stops between assets or is discarded at
        /// delivery. Cancelled tiles appear in neither `drain()` nor
        /// `drain_failures()`. No-op if the key is not in flight.
        void cancel(const tile_key &key);

        /// Returns (and clears) all payloads completed since the last call.
        /// Single lock + swap; call once per frame from the main thread.
        [[nodiscard]] std::vector<tile_payload> drain();

        /// Returns (and clears) keys whose job failed (network / decode).
        /// The caller should forget them; they are not retried internally.
        [[nodiscard]] std::vector<tile_key> drain_failures();

    private:
        struct job {
            tile_key key;
            int abs_x = 0;
            int abs_z = 0;
        };

        void worker_loop(const std::stop_token &st);

        source_options options;
        std::vector<std::jthread> workers;

        // all five containers below are guarded by mtx
        std::queue<job> queue_;
        std::unordered_set<tile_key> in_flight_; // requested, not yet delivered/failed/cancelled
        std::unordered_set<tile_key> cancelled_; // subset of in_flight_ marked for discard
        std::vector<tile_payload> ready_;        // completed payloads awaiting drain()
        std::vector<tile_key> failed_;           // failed keys awaiting drain_failures()

        std::mutex mtx;
        // condition_variable_any (not std::condition_variable!) is required so we can
        // use the 3-arg wait(lock, stop_token, predicate) overload that cooperates
        // with std::jthread::request_stop(). do not "simplify" to condition_variable
        // - workers would never wake up on shutdown and ~tile_source would hang forever.
        std::condition_variable_any cv;
    };
} // namespace raytiles
