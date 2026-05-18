#pragma once
#include <condition_variable>
#include <iostream>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <map>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_set>
#include <thread>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>
#include <emscripten.h>
#else
#if defined(_WIN32)
#define NOGDI
#define NOUSER
#endif
#include "httplib.h"
#endif

#include "raylib.h"
// stb_image function prototypes only — the implementation lives inside
// raylib (rtextures.o exports stbi_load_from_memory / stbi_image_free with
// external linkage). raylib's LoadImageFromMemory itself is NOT thread-safe
// (it routes through TraceLog and other globals), but the stb_image core
// is, so workers call into it directly.
#define STBI_ONLY_PNG
#define STBI_NO_FAILURE_STRINGS
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#include "external/stb_image.h"

namespace raytiles {
    // replaces first occurrence by design
    static void replace(std::string &str, const std::string &from, const std::string &to) {
        if (from.empty()) return;
        const size_t pos = str.find(from);
        if (pos == std::string::npos) return;
        str.replace(pos, from.length(), to);
    }

    static std::string get_url(std::string url, const int zoom, const int x, const int y) {
        replace(url, ":zoom:", std::to_string(zoom));
        replace(url, ":x:", std::to_string(x));
        replace(url, ":y:", std::to_string(y));
        return url;
    }

    static std::pair<std::string, std::string> split_url(const std::string &url) {
        const size_t search_start = url.find("://") + 3;
        const size_t path_pos = url.find('/', search_start);

        if (path_pos == std::string::npos) return {url, ""};

        return {url.substr(0, path_pos), url.substr(path_pos)};
    }

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
        // tiny thread-safe logger: serializes writes to stderr behind a single mutex
        // so log lines never interleave. used in workers in place of TraceLog.
        void log_line(const std::string_view level, const std::string_view msg) const {
            if (!config.use_logger) return;
            static std::mutex log_mtx;
            std::lock_guard lock(log_mtx);
            std::fprintf(stderr, "[%.*s] %.*s\n", static_cast<int>(level.size()), level.data(), static_cast<int>(msg.size()), msg.data());
            std::fflush(stderr);
        }

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

        pool_config config;
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

#ifndef __EMSCRIPTEN__
        [[nodiscard]] httplib::Client create_client(const std::string &host) const {
            httplib::Client cli(host);
            cli.set_follow_location(true);
            cli.set_connection_timeout(5);
            cli.set_read_timeout(3);
            cli.set_keep_alive(true);
            cli.enable_server_certificate_verification(!config.allow_insecure_tls);
            return cli;
        }
#endif

        [[nodiscard]] std::string get_host(const request_type type) const {
            return type == TEXTURE ? config.texture_host : type == HEIGHTMAP ? config.heightmap_host : config.normals_host;
        }

        void worker_loop(const std::stop_token &st) {
#ifndef __EMSCRIPTEN__

            // persistent http clients per host. endpoints all live under a
            // single host, so we can keep the TLS connection alive across many tiles
            // instead of paying handshake cost per fetch. the clients are owned by the
            // worker thread so no synchronization is needed.
            std::unordered_map<std::string, httplib::Client> clients;

#endif

            while (true) {
                ImageJob img_job;
                bool is_cancelled = false;
                {
                    std::unique_lock lock(mtx);
                    if (!cv.wait(lock, st, [this] { return !image_queue.empty(); })) return;
                    img_job = std::move(image_queue.front());
                    image_queue.pop();

                    if (cancelled_jobs.contains(img_job.path)) {
                        cancelled_jobs.erase(img_job.path);
                        in_flight_images.erase(img_job.path);
                        is_cancelled = true;
                    }
                }

                if (is_cancelled) {
                    try {
                        throw std::runtime_error("tile download cancelled");
                    } catch (...) {
                        img_job.promise.set_exception(std::current_exception());
                    }
                    continue;
                }

                try {
                    std::string bytes;

                    if (std::filesystem::exists(img_job.path)) {
                        bytes = read_file(img_job.path);
                        log_line("DEBUG", std::format("tile loaded from cache: {}", img_job.path));
                    } else {
                        auto host = get_host(img_job.type);
#ifdef __EMSCRIPTEN__
                        auto body = fetch(host + img_job.url);
#else
                        // todo do we need to destroy the hosts when done?
                        if (!clients.contains(host)) clients.try_emplace(host, create_client(host));
                        auto body = fetch(clients.at(host), img_job.url);
#endif

                        log_line("DEBUG", std::format("tile downloaded: {} ", img_job.path));
                        write_atomic(img_job.path, body);
                        log_line("DEBUG", std::format("tile cached: {} ", img_job.path));
                        bytes = std::move(body);
                    }

                    // decode PNG to a raylib Image off the main thread. raylib's
                    // LoadImageFromMemory is not thread-safe (it routes through
                    // TraceLog and other globals), so we call stb_image directly.
                    // ownership of the malloc'd pixel buffer transfers to whoever
                    // consumes the future via .get(); they must free it with
                    // UnloadImage (or wrap it in raii::image).
                    Image img = decode_png(bytes);
                    img_job.promise.set_value(img);
                } catch (...) {
                    try {
                        img_job.promise.set_exception(std::current_exception());
                    } catch (...) {
                        // promise already satisfied or broken; nothing else we can do
                    }
                }
                {
                    std::lock_guard lock(mtx);
                    in_flight_images.erase(img_job.path);
                }
            }
        }

        // decode a PNG byte buffer into a raylib Image. pixels are allocated
        // with stb_image's allocator (malloc) which matches raylib's default
        // RL_FREE, so UnloadImage is the correct deleter on the result.
        // throws std::runtime_error on decode failure or unsupported channel
        // count (only 3 / 4 channel inputs are supported, matching the
        // formats utils::get_height_from_image accepts).
        static Image decode_png(const std::string &bytes) {
            int w = 0, h = 0, comp = 0;
            stbi_uc *data = stbi_load_from_memory(
                reinterpret_cast<const stbi_uc *>(bytes.data()),
                static_cast<int>(bytes.size()),
                &w, &h, &comp, 0);
            if (!data) throw std::runtime_error("PNG decode failed");
            if (comp != 3 && comp != 4) {
                stbi_image_free(data);
                throw std::runtime_error(std::format("unsupported PNG channel count: {}", comp));
            }
            Image img{};
            img.data = data;
            img.width = w;
            img.height = h;
            img.mipmaps = 1;
            img.format = (comp == 4) ? PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
                                     : PIXELFORMAT_UNCOMPRESSED_R8G8B8;
            return img;
        }

        static std::string read_file(const std::string &path) {
            std::ifstream f(path, std::ios::binary);
            if (!f) {
                throw std::runtime_error("failed to open cached file: " + path);
            }
            std::string out((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            return out;
        }

#ifdef __EMSCRIPTEN__
        static std::string fetch(const std::string &url) {
            std::string result = "";
            emscripten_fetch_attr_t attr;
            emscripten_fetch_attr_init(&attr);
            strcpy(attr.requestMethod, "GET");

            attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_SYNCHRONOUS;
            emscripten_fetch_t *fetch = emscripten_fetch(&attr, url.c_str());

            if (fetch != nullptr) {
                if (fetch->status == 200) {
                    result = std::string(fetch->data, fetch->numBytes);
                } else {
                    std::cerr << "Web Download Error: HTTP " << fetch->status << " for URL: " << url << std::endl;
                }
                emscripten_fetch_close(fetch);
            } else {
                std::cerr << "Emscripten fetch initialization failed for URL: " << url << std::endl;
            }
            return result;
        }
#else

        static std::string fetch(httplib::Client &cli, const std::string &url) {
            auto res = cli.Get(url);
            if (!res || res->status != 200) {
                const int status = res ? res->status : -1;
                const std::string err = res ? std::string{} : httplib::to_string(res.error());
                throw std::runtime_error(std::format("download failed: {} status={} err={}", url, status, err));
            }
            return std::move(res->body);
        }
#endif

        static void write_atomic(const std::string &path, const std::string &bytes) {
            std::filesystem::create_directories(std::filesystem::path(path).parent_path());
            const std::string tmp_path = path + ".tmp";
            std::FILE *f = std::fopen(tmp_path.c_str(), "wb");
            if (!f) {
                throw std::runtime_error("fopen failed: " + tmp_path);
            }
            std::fwrite(bytes.data(), 1, bytes.size(), f);
            std::fclose(f);
            std::error_code ec;
            std::filesystem::rename(tmp_path, path, ec);
            if (ec) {
                throw std::runtime_error("rename failed: " + path);
            }
        }

    public:
        explicit pool(pool_config conf)
            : config(std::move(conf)) {
            // normalize configuration
            // take the URLs and split them to 2 parts, the host part and path part
            // todo input validation
            auto [texture_host, texture_path] = split_url(config.texture_url);
            config.texture_host = std::move(texture_host);
            config.texture_url_path = std::move(texture_path);

            auto [heightmap_host, heightmap_path] = split_url(config.heightmap_url);
            config.heightmap_host = std::move(heightmap_host);
            config.heightmap_url_path = std::move(heightmap_path);

            auto [normals_host, normals_path] = split_url(config.normals_url);
            config.normals_host = std::move(normals_host);
            config.normals_url_path = std::move(normals_path);

            workers.reserve(config.download_threads);
            for (int i = 0; i < config.download_threads; ++i) workers.emplace_back([this](const std::stop_token &st) { worker_loop(st); });
        }

        // destructing the streamer may block while in-flight downloads time out.
        ~pool() {
            for (auto &w: workers) w.request_stop();
            cv.notify_all();
            workers.clear();
        }

        // Cancel job only if not already picked up
        void cancel_load(const std::string &path) {
            std::lock_guard lock(mtx);
            if (in_flight_images.contains(path)) {
                cancelled_jobs.insert(path);
            }
        }

        void cancel_texture(int zoom, int x, int y) {
            cancel_load(std::vformat(config.texture_cache_path, std::make_format_args(zoom, x, y)));
        }

        void cancel_heightmap(int zoom, int x, int y) {
            cancel_load(std::vformat(config.heightmap_cache_path, std::make_format_args(zoom, x, y)));
        }

        void cancel_normals(int zoom, int x, int y) {
            cancel_load(std::vformat(config.normals_cache_path, std::make_format_args(zoom, x, y)));
        }

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
        std::shared_future<Image> enqueue_and_load(const std::string &path, const std::string &url, const request_type type = TEXTURE) {
            std::lock_guard lock(mtx);

            // make sure it is not canceled
            cancelled_jobs.erase(path);

            if (const auto it = in_flight_images.find(path); it != in_flight_images.end()) return it->second;

            std::promise<Image> promise;
            std::shared_future<Image> future = promise.get_future().share();
            in_flight_images.emplace(path, future);
            image_queue.push({type, path, url, std::move(promise)});
            cv.notify_one();
            return future;
        }

        std::shared_future<Image> enqueue_texture(int zoom, int x, int y) {
            const auto path = std::vformat(config.texture_cache_path, std::make_format_args(zoom, x, y));
            return enqueue_and_load(path, get_url(config.texture_url_path, zoom, x, y), TEXTURE);
        }

        std::shared_future<Image> enqueue_heightmap(int zoom, int x, int y) {
            const auto path = std::vformat(config.heightmap_cache_path, std::make_format_args(zoom, x, y));
            return enqueue_and_load(path, get_url(config.heightmap_url_path, zoom, x, y), HEIGHTMAP);
        }

        std::shared_future<Image> enqueue_normals(int zoom, int x, int y) {
            const auto path = std::vformat(config.normals_cache_path, std::make_format_args(zoom, x, y));
            return enqueue_and_load(path, get_url(config.normals_url_path, zoom, x, y), NORMALS);
        }
    };
} // namespace raytiles
