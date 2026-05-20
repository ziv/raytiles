#include "raytiles/raytiles.h"
#include "detail/downloader.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

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
    namespace {
        // replaces first occurrence by design
        void replace(std::string &str, const std::string &from, const std::string &to) {
            if (from.empty()) return;
            const size_t pos = str.find(from);
            if (pos == std::string::npos) return;
            str.replace(pos, from.length(), to);
        }

        std::string get_url(std::string url, const int zoom, const int x, const int y) {
            replace(url, ":zoom:", std::to_string(zoom));
            replace(url, ":x:", std::to_string(x));
            replace(url, ":y:", std::to_string(y));
            return url;
        }

        std::pair<std::string, std::string> split_url(const std::string &url) {
            const auto scheme = url.find("://");
            if (scheme == std::string::npos) throw std::runtime_error("invalid url (no scheme): " + url);

            const auto path_pos = url.find('/', scheme + 3);
            if (path_pos == std::string::npos) return {url, "/"};

            return {url.substr(0, path_pos), url.substr(path_pos)};
        }

        // decode a PNG byte buffer into a raylib Image. pixels are allocated
        // with stb_image's allocator (malloc) which matches raylib's default
        // RL_FREE, so UnloadImage is the correct deleter on the result.
        // throws std::runtime_error on decode failure or unsupported channel
        // count (only 3 / 4 channel inputs are supported, matching the
        // formats utils::get_height_from_image accepts).
        Image decode_png(const std::string &bytes) {
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
            img.format = (comp == 4)
                             ? PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
                             : PIXELFORMAT_UNCOMPRESSED_R8G8B8;
            return img;
        }

        std::string read_file(const std::string &path) {
            std::ifstream f(path, std::ios::binary);
            if (!f) {
                throw std::runtime_error("failed to open cached file: " + path);
            }
            std::string out((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            return out;
        }

#ifdef __EMSCRIPTEN__
        std::string fetch(const std::string &url) {
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
        std::string fetch(httplib::Client &cli, const std::string &url) {
            auto res = cli.Get(url);
            if (!res || res->status != 200) {
                const int status = res ? res->status : -1;
                const std::string err = res ? std::string{} : httplib::to_string(res.error());
                throw std::runtime_error(std::format("download failed: {} status={} err={}", url, status, err));
            }
            return std::move(res->body);
        }

        [[nodiscard]] httplib::Client create_client(const std::string &host, const bool allow_insecure_tls) {
            httplib::Client cli(host);
            cli.set_follow_location(true);
            cli.set_connection_timeout(5);
            cli.set_read_timeout(3);
            cli.set_keep_alive(true);
            cli.enable_server_certificate_verification(!allow_insecure_tls);
            return cli;
        }
#endif

        void write_atomic(const std::string &path, const std::string &bytes) {
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
    } // namespace

    void pool::log_line(const std::string_view level, const std::string_view msg) const {
        if (!config.use_logger) return;
        static std::mutex log_mtx;
        std::lock_guard lock(log_mtx);
        std::fprintf(stderr, "[%.*s] %.*s\n", static_cast<int>(level.size()), level.data(), static_cast<int>(msg.size()), msg.data());
        std::fflush(stderr);
    }

    std::string pool::get_host(const request_type type) const {
        return type == TEXTURE ? config.texture_host : type == HEIGHTMAP ? config.heightmap_host : config.normals_host;
    }

    void pool::worker_loop(const std::stop_token &st) {
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
                img_job.promise.set_exception(
                    std::make_exception_ptr(std::runtime_error("tile download cancelled"))
                );
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
                    if (!clients.contains(host)) clients.try_emplace(host, create_client(host, config.allow_insecure_tls));
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
                {
                    std::lock_guard lock(mtx);
                    in_flight_images.erase(img_job.path);
                }
                img_job.promise.set_value(img);
            } catch (...) {
                try {
                    img_job.promise.set_exception(std::current_exception());
                } catch (...) {
                    // promise already satisfied or broken; nothing else we can do
                }
            }
        }
    }

    pool::pool(pool_config conf)
        : config(std::move(conf)) {
        // normalize configuration
        // take the URLs and split them to 2 parts, the host part and path part
        // todo input validation (FOR EVERYTHING)
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

    pool::~pool() {
        for (auto &w: workers) w.request_stop();
        cv.notify_all();
        workers.clear();
    }

    void pool::cancel_load(const std::string &path) {
        std::lock_guard lock(mtx);
        if (in_flight_images.contains(path)) {
            cancelled_jobs.insert(path);
        }
    }

    void pool::cancel_texture(int zoom, int x, int y) {
        cancel_load(std::vformat(config.texture_cache_path, std::make_format_args(zoom, x, y)));
    }

    void pool::cancel_heightmap(int zoom, int x, int y) {
        cancel_load(std::vformat(config.heightmap_cache_path, std::make_format_args(zoom, x, y)));
    }

    void pool::cancel_normals(int zoom, int x, int y) {
        cancel_load(std::vformat(config.normals_cache_path, std::make_format_args(zoom, x, y)));
    }

    void pool::cancel(const int zoom, const int x, const int y) {
        cancel_texture(zoom, x, y);
        cancel_heightmap(zoom, x, y);
        cancel_normals(zoom, x, y);
    }

    std::shared_future<Image> pool::enqueue_and_load(const std::string &path, const std::string &url, const request_type type) {
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

    std::shared_future<Image> pool::enqueue_texture(int zoom, int x, int y) {
        const auto path = std::vformat(config.texture_cache_path, std::make_format_args(zoom, x, y));
        return enqueue_and_load(path, get_url(config.texture_url_path, zoom, x, y), TEXTURE);
    }

    std::shared_future<Image> pool::enqueue_heightmap(int zoom, int x, int y) {
        const auto path = std::vformat(config.heightmap_cache_path, std::make_format_args(zoom, x, y));
        return enqueue_and_load(path, get_url(config.heightmap_url_path, zoom, x, y), HEIGHTMAP);
    }

    std::shared_future<Image> pool::enqueue_normals(int zoom, int x, int y) {
        const auto path = std::vformat(config.normals_cache_path, std::make_format_args(zoom, x, y));
        return enqueue_and_load(path, get_url(config.normals_url_path, zoom, x, y), NORMALS);
    }
} // namespace raytiles
