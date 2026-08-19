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
#include <vector>

#include "raylib.h"
#include "raii.hpp"
#include "tile.hpp"

namespace raytiles {
    struct tile_source_options {
        int download_threads = 4;
        bool allow_insecure_tls = false;

        /// HTTP timeouts (seconds). Defaults match the previous hardcoded
        /// values; the unit tests shorten them.
        int connection_timeout_sec = 5;
        int read_timeout_sec = 3;

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

        explicit tile_source(tile_source_options opts);

        /// Blocks until workers exit; an in-flight HTTP fetch cannot be
        /// aborted, so this may wait out a network timeout.
        ~tile_source();

        tile_source(const tile_source &) = delete;

        tile_source &operator=(const tile_source &) = delete;

        /// Queues one tile fetch. Dedup by key: a no-op while the same key is
        /// pending or executing (a cancelled-but-not-yet-collected job counts
        /// as in flight; its drop must be drained before the key can be
        /// requested again).
        void request(const tile_request &req);

        /// Flags the job for cancellation (checked at pickup and between
        /// assets). The key is always answered — either by a payload that was
        /// already past the last check, or by a cancelled drop.
        void cancel(const tile_key &key);

        /// Collects everything finished since the last drain: clears both out
        /// params and swaps the internal queues into them (one lock, zero
        /// allocations in steady state when the caller reuses its vectors).
        void drain(std::vector<tile_payload> &ready_out, std::vector<drop> &dropped_out);

    private:
        enum request_type {
            TEXTURE,
            HEIGHTMAP,
            NORMALS
        };

        struct job {
            tile_request req;
            std::shared_ptr<std::atomic_bool> cancelled;
        };

        void worker_loop(const std::stop_token &st);

        /// Erase from in_flight and record the drop, under the lock.
        void deliver_drop(const tile_key &key, bool cancelled, std::string reason);

        tile_source_options options;
        std::vector<std::jthread> workers;

        std::queue<job> pending;
        std::unordered_map<tile_key, std::shared_ptr<std::atomic_bool> > in_flight;
        std::vector<tile_payload> ready;
        std::vector<drop> dropped;
        std::mutex mtx;
        std::condition_variable_any cv;
    };
} // namespace raytiles
