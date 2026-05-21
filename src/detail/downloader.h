#pragma once

#include <condition_variable>
#include <future>
#include <map>
#include <mutex>
#include <queue>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "raylib.h"

namespace raytiles {
    struct pool_options {
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

    // pool of background workers. each job downloads a tile (or reads it from the
    // on-disk cache), decodes the PNG bytes to a raylib Image with stb_image,
    // and resolves a future with that Image. raylib's higher-level API is not
    // thread-safe (image decoders, TextFormat/TextToLower static buffers,
    // internal trace state including TraceLog itself), so workers must NEVER
    // touch any raylib function. stb_image is reentrant (with
    // STBI_NO_THREAD_LOCALS) and produces a malloc'd pixel buffer that the
    // main thread can hand off to UnloadImage / RL_FREE later. GPU upload
    // still happens on the main thread.
    class pool {
        enum request_type {
            TEXTURE,
            HEIGHTMAP,
            NORMALS
        };

        struct ImageJob {
            request_type type = TEXTURE;
            std::string path;
            std::string url;
            std::promise<Image> promise;
        };

        pool_options options;
        std::vector<std::jthread> workers;
        std::queue<ImageJob> image_queue;
        std::map<std::string, std::shared_future<Image> > in_flight_images;
        std::unordered_set<std::string> cancelled_jobs;
        std::mutex mtx;
        // condition_variable_any (not std::condition_variable!) is required so we can
        // use the 3-arg wait(lock, stop_token, predicate) overload that cooperates
        // with std::jthread::request_stop(). do not "simplify" to condition_variable
        // - workers would never wake up on shutdown and ~pool would hang forever.
        std::condition_variable_any cv;

        // tiny thread-safe logger: serializes writes to stderr behind a single mutex
        // so log lines never interleave. used in workers in place of TraceLog.
        void log_line(std::string_view level, std::string_view msg) ;

        [[nodiscard]] std::string get_host(request_type type) const;

        void worker_loop(const std::stop_token &st);

    public:
        explicit pool(pool_options opts);

        // destructing the streamer may block while in-flight downloads time out.
        ~pool();

        // Cancel job only if not already picked up
        void cancel_load(const std::string &path);

        void cancel_texture(int zoom, int x, int y);

        void cancel_heightmap(int zoom, int x, int y);

        void cancel_normals(int zoom, int x, int y);

        void cancel(int zoom, int x, int y);

        // returns a shared_future that resolves with a decoded raylib Image.
        // the worker thread reads the tile from disk (or downloads it), then
        // decodes the PNG bytes with stb_image (raylib's image decoders are
        // not thread-safe). the consumer that calls .get() takes ownership of
        // the Image's pixel buffer and must release it with UnloadImage
        // (typically by wrapping in raii::image, or after uploading to GPU).
        //
        // race note: the worker calls promise.set_value() before erasing the
        // entry from in_flight_images. if a second caller requests the same
        // path during that window, it receives a future that is already
        // satisfied - which is desirable (no duplicate download). but note
        // that BOTH copies of the future would then hand out the same Image
        // pointer via .get(); current callers only .get() once per loading
        // tile so this is safe in practice.
        //
        // rare race may cause a single tile to be re-read from cache
        std::shared_future<Image> enqueue_and_load(const std::string &path, const std::string &url, request_type type = TEXTURE);

        std::shared_future<Image> enqueue_texture(int zoom, int x, int y);

        std::shared_future<Image> enqueue_heightmap(int zoom, int x, int y);

        std::shared_future<Image> enqueue_normals(int zoom, int x, int y);
    };
} // namespace raytiles
