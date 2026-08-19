#include "detail/tile_source.h"
#include "detail/utils.hpp" // build_height_grid (pure CPU math, worker-safe)

#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

#if defined(_WIN32)
#define NOGDI
#define NOUSER
#endif
#include "httplib.h"

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

        std::string fetch(httplib::Client &cli, const std::string &url) {
            auto res = cli.Get(url);
            if (!res || res->status != 200) {
                const int status = res ? res->status : -1;
                const std::string err = res ? std::string{} : httplib::to_string(res.error());
                throw std::runtime_error(std::format("download failed: {} status={} err={}", url, status, err));
            }
            return std::move(res->body);
        }

        [[nodiscard]] httplib::Client create_client(const std::string &host, const tile_source_options &opts) {
            httplib::Client cli(host);
            cli.set_follow_location(true);
            cli.set_connection_timeout(opts.connection_timeout_sec);
            cli.set_read_timeout(opts.read_timeout_sec);
            cli.set_keep_alive(true);
            cli.enable_server_certificate_verification(!opts.allow_insecure_tls);
            return cli;
        }

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

        // worker-side free that must not touch raylib: pixel buffers come from
        // stb_image, so stbi_image_free is the matching deleter
        void free_partial(Image &img) {
            if (img.data != nullptr) {
                stbi_image_free(img.data);
                img.data = nullptr;
            }
        }
    } // namespace

    tile_source::tile_source(tile_source_options opts)
        : options(std::move(opts)) {
        workers.reserve(static_cast<std::size_t>(options.download_threads));
        for (int i = 0; i < options.download_threads; ++i) workers.emplace_back([this](const std::stop_token &st) { worker_loop(st); });
    }

    tile_source::~tile_source() {
        for (auto &w: workers) w.request_stop();
        cv.notify_all();
        workers.clear(); // joins; pending jobs and undrained payloads die on this (main) thread
    }

    void tile_source::request(const tile_request &req) {
        std::lock_guard lock(mtx);
        if (in_flight.contains(req.key)) return;

        auto flag = std::make_shared<std::atomic_bool>(false);
        in_flight.emplace(req.key, flag);
        pending.push(job{req, std::move(flag)});
        cv.notify_one();
    }

    void tile_source::cancel(const tile_key &key) {
        std::lock_guard lock(mtx);
        if (const auto it = in_flight.find(key); it != in_flight.end()) {
            it->second->store(true, std::memory_order_relaxed);
        }
    }

    void tile_source::drain(std::vector<tile_payload> &ready_out, std::vector<drop> &dropped_out) {
        ready_out.clear();
        dropped_out.clear();
        std::lock_guard lock(mtx);
        std::swap(ready, ready_out);
        std::swap(dropped, dropped_out);
    }

    void tile_source::deliver_drop(const tile_key &key, const bool cancelled, std::string reason) {
        std::lock_guard lock(mtx);
        in_flight.erase(key);
        dropped.push_back(drop{key, cancelled, std::move(reason)});
    }

    void tile_source::worker_loop(const std::stop_token &st) {
        // persistent http clients per host. endpoints usually live under a
        // single host, so the TLS connection stays alive across many tiles
        // instead of paying handshake cost per fetch. owned by the worker
        // thread, so no synchronization is needed.
        std::unordered_map<std::string, httplib::Client> clients;

        // cache-or-network fetch of one asset, decoded to a CPU image
        const auto fetch_asset = [&](const std::string &cache_template, const std::string &host, const std::string &url_path,
                                     const tile_request &req) -> Image {
            const auto path = std::vformat(cache_template, std::make_format_args(req.key.zoom, req.x, req.z));
            std::string bytes;
            if (std::filesystem::exists(path)) {
                bytes = read_file(path);
            } else {
                if (!clients.contains(host)) clients.try_emplace(host, create_client(host, options));
                auto body = fetch(clients.at(host), get_url(url_path, req.key.zoom, req.x, req.z));
                write_atomic(path, body);
                bytes = std::move(body);
            }
            return decode_png(bytes);
        };

        while (true) {
            job j;
            {
                std::unique_lock lock(mtx);
                if (!cv.wait(lock, st, [this] { return !pending.empty(); })) return;
                j = std::move(pending.front());
                pending.pop();
            }

            const auto is_cancelled = [&] { return j.cancelled->load(std::memory_order_relaxed); };

            if (is_cancelled()) {
                deliver_drop(j.req.key, true, {});
                continue;
            }

            Image tex{}, hm{}, nl{};
            try {
                tex = fetch_asset(options.texture_cache_path, options.texture_host, options.texture_url_path, j.req);
                if (!is_cancelled()) {
                    hm = fetch_asset(options.heightmap_cache_path, options.heightmap_host, options.heightmap_url_path, j.req);
                }
                if (!is_cancelled()) {
                    nl = fetch_asset(options.normals_cache_path, options.normals_host, options.normals_url_path, j.req);
                }
            } catch (const std::exception &e) {
                free_partial(tex);
                free_partial(hm);
                free_partial(nl);
                deliver_drop(j.req.key, false, e.what());
                continue;
            }

            if (is_cancelled()) {
                free_partial(tex);
                free_partial(hm);
                free_partial(nl);
                deliver_drop(j.req.key, true, {});
                continue;
            }

            // derive the CPU query grid here so the main thread's upload
            // budget never pays for it (pure math, no raylib)
            height_grid heights = utils::build_height_grid(hm);

            std::lock_guard lock(mtx);
            in_flight.erase(j.req.key);
            ready.push_back(tile_payload{j.req.key, raii::image{tex}, raii::image{hm}, raii::image{nl}, std::move(heights)});
        }
    }
} // namespace raytiles
