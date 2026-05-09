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

namespace raytiles {
    // todo we don't need replace all
    // todo performance
    static void replace_all(std::string &str, const std::string &from, const std::string &to) {
        if (from.empty()) return;
        size_t start_pos = 0;

        while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
            str.replace(start_pos, from.length(), to);
            start_pos += to.length();
        }
    }

    static std::string get_url(std::string url, const int zoom, const int x, const int y) {
        replace_all(url, "{zoom}", std::to_string(zoom));
        replace_all(url, "{x}", std::to_string(x));
        replace_all(url, "{y}", std::to_string(y));
        return url;
    }

    // pool of background workers. each job downloads a tile (or reads it from the
    // on-disk cache) and resolves a future with the raw file bytes. raylib is not
    // thread-safe (image decoders, TextFormat/TextToLower static buffers, internal
    // trace state including TraceLog itself), so workers must NEVER touch any
    // raylib API. all decoding is deferred to the main thread once the future
    // resolves.
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
            std::promise<std::string> promise;
        };

        pool_config config;
        std::vector<std::jthread> workers;
        std::queue<ImageJob> image_queue;
        std::map<std::string, std::shared_future<std::string> > in_flight_bytes;
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
            cli.set_connection_timeout(10);
            cli.set_read_timeout(5);
            cli.set_keep_alive(true);
            cli.enable_server_certificate_verification(!config.allow_insecure_tls);
            return cli;
        }
#endif

        [[nodiscard]] std::string get_host(const request_type type) const {
            return type == TEXTURE ? config.texture_host : type == HEIGHTMAP ? config.heightmap_host : config.normals_host;
        }

        void worker_loop(const std::stop_token &st) {
#ifdef __EMSCRIPTEN__
#else

            // persistent http clients per host. endpoints all live under a
            // single host, so we can keep the TLS connection alive across many tiles
            // instead of paying handshake cost per fetch. the clients are owned by the
            // worker thread so no synchronization is needed.
            std::unordered_map<std::string, httplib::Client> clients;

#endif

            while (true) {
                ImageJob img_job;
                {
                    std::unique_lock lock(mtx);
                    if (!cv.wait(lock, st, [this] { return !image_queue.empty(); })) return;
                    img_job = std::move(image_queue.front());
                    image_queue.pop();
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
                    img_job.promise.set_value(std::move(bytes));
                } catch (...) {
                    try {
                        img_job.promise.set_exception(std::current_exception());
                    } catch (...) {
                        // promise already satisfied or broken; nothing else we can do
                    }
                }
                {
                    std::lock_guard lock(mtx);
                    in_flight_bytes.erase(img_job.path);
                }
            }
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
            workers.reserve(config.download_threads);
            for (int i = 0; i < config.download_threads; ++i) workers.emplace_back([this](const std::stop_token &st) { worker_loop(st); });
        }

        ~pool() {
            for (auto &w: workers) w.request_stop();
            workers.clear();
        }

        // returns a shared_future that resolves with the raw file bytes after download
        // (or after reading from cache). decoding into an Image is the caller's
        // responsibility and must be done on the main thread.
        //
        // race note: the worker calls promise.set_value() before erasing the entry
        // from in_flight_bytes. if a second caller requests the same path during
        // that window, it receives a future that is already satisfied - which is
        // exactly the desired behavior (the bytes are immediately available, no
        // duplicate download is queued).
        std::shared_future<std::string> enqueue_and_load(const std::string &path, const std::string &url, const request_type type = TEXTURE) {
            std::lock_guard lock(mtx);
            if (const auto it = in_flight_bytes.find(path); it != in_flight_bytes.end()) return it->second;

            std::promise<std::string> promise;
            std::shared_future<std::string> future = promise.get_future().share();
            in_flight_bytes.emplace(path, future);
            image_queue.push({type, path, url, std::move(promise)});
            cv.notify_one();
            return future;
        }

        std::shared_future<std::string> enqueue_texture(int zoom, int x, int y) {
            const auto path = std::vformat(config.texture_cache_path, std::make_format_args(zoom, x, y));
            return enqueue_and_load(path, get_url(config.texture_url_path, zoom, x, y), TEXTURE);
        }

        std::shared_future<std::string> enqueue_heightmap(int zoom, int x, int y) {
            const auto path = std::vformat(config.heightmap_cache_path, std::make_format_args(zoom, x, y));
            return enqueue_and_load(path, get_url(config.heightmap_url_path, zoom, x, y), HEIGHTMAP);
        }

        std::shared_future<std::string> enqueue_normals(int zoom, int x, int y) {
            const auto path = std::vformat(config.normals_cache_path, std::make_format_args(zoom, x, y));
            return enqueue_and_load(path, get_url(config.normals_url_path, zoom, x, y), NORMALS);
        }
    };
} // namespace raytiles
