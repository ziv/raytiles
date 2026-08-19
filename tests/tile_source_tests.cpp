/// Tests for tile_source (src/detail/tile_source.h): the real worker pool
/// driven against pre-seeded disk caches and a local httplib server. Fully
/// offline and windowless — only CPU-side raylib calls (image authoring).
#include "doctest.h"

#include "detail/tile_source.h"

#if defined(_WIN32)
#define NOGDI
#define NOUSER
#endif
#include "httplib.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using raytiles::tile_key;
using raytiles::tile_payload;
using raytiles::tile_request;
using raytiles::tile_source;
using raytiles::tile_source_options;

namespace {
    namespace fs = std::filesystem;

    // valid PNG bytes (2x2 RGBA), authored once via raylib's CPU-side exporter
    const std::string &png_bytes() {
        static const std::string bytes = [] {
            const Image img = GenImageColor(2, 2, RED);
            int size = 0;
            unsigned char *data = ExportImageToMemory(img, ".png", &size);
            std::string out(reinterpret_cast<const char *>(data), static_cast<std::size_t>(size));
            MemFree(data);
            UnloadImage(img);
            return out;
        }();
        return bytes;
    }

    fs::path fresh_temp_dir() {
        static std::atomic<int> counter{0};
        const auto dir = fs::temp_directory_path() / std::format("raytiles_source_test_{}", counter.fetch_add(1));
        fs::remove_all(dir);
        fs::create_directories(dir);
        return dir;
    }

    void write_file(const fs::path &path, const std::string &bytes) {
        fs::create_directories(path.parent_path());
        std::ofstream f(path, std::ios::binary);
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    // options rooted in `dir`, pointing at a host that refuses connections;
    // tests that want HTTP override the host with a live local server
    tile_source_options make_options(const fs::path &dir, const std::string &host = "http://127.0.0.1:9") {
        tile_source_options opts;
        opts.download_threads = 1; // deterministic pickup order for the cancel/dedup tests
        opts.connection_timeout_sec = 1;
        opts.read_timeout_sec = 5; // above the slow-route sleep so slow != timeout
        opts.texture_cache_path = (dir / "tex" / "{}" / "{}" / "{}.png").string();
        opts.heightmap_cache_path = (dir / "hm" / "{}" / "{}" / "{}.png").string();
        opts.normals_cache_path = (dir / "nl" / "{}" / "{}" / "{}.png").string();
        opts.texture_host = host;
        opts.heightmap_host = host;
        opts.normals_host = host;
        opts.texture_url_path = "/tex/:zoom:/:x:/:y:.png";
        opts.heightmap_url_path = "/hm/:zoom:/:x:/:y:.png";
        opts.normals_url_path = "/nl/:zoom:/:x:/:y:.png";
        return opts;
    }

    void seed_cache(const tile_source_options &opts, const int zoom, const int x, const int z, const std::string &bytes) {
        write_file(std::vformat(opts.texture_cache_path, std::make_format_args(zoom, x, z)), bytes);
        write_file(std::vformat(opts.heightmap_cache_path, std::make_format_args(zoom, x, z)), bytes);
        write_file(std::vformat(opts.normals_cache_path, std::make_format_args(zoom, x, z)), bytes);
    }

    // drains the source until `pred` holds or the deadline passes
    struct harness {
        tile_source src;
        std::vector<tile_payload> payloads;
        std::vector<tile_source::drop> drops;

        explicit harness(tile_source_options opts) : src(std::move(opts)) {
        }

        template<typename Pred>
        bool pump_until(Pred pred, const std::chrono::milliseconds timeout = 5000ms) {
            std::vector<tile_payload> ready;
            std::vector<tile_source::drop> dropped;
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            while (true) {
                src.drain(ready, dropped);
                for (auto &p: ready) payloads.push_back(std::move(p));
                for (auto &d: dropped) drops.push_back(std::move(d));
                if (pred()) return true;
                if (std::chrono::steady_clock::now() >= deadline) return false;
                std::this_thread::sleep_for(5ms);
            }
        }
    };

    struct test_server {
        httplib::Server svr;
        int port = 0;
        std::thread thread;

        test_server() {
            // serve any /tex|/hm|/nl asset; /slow/... sleeps first (cancel window)
            const auto serve = [](const httplib::Request &, httplib::Response &res) {
                res.set_content(png_bytes(), "image/png");
            };
            svr.Get(R"(/tex/.*)", serve);
            svr.Get(R"(/hm/.*)", serve);
            svr.Get(R"(/nl/.*)", serve);
            svr.Get(R"(/slow/.*)", [&](const httplib::Request &req, httplib::Response &res) {
                std::this_thread::sleep_for(2000ms);
                serve(req, res);
            });
            port = svr.bind_to_any_port("127.0.0.1");
            thread = std::thread([this] { svr.listen_after_bind(); });
            svr.wait_until_ready();
        }

        ~test_server() {
            svr.stop();
            thread.join();
        }

        [[nodiscard]] std::string host() const { return std::format("http://127.0.0.1:{}", port); }
    };
} // namespace

TEST_CASE("cache hit delivers a full payload without any network") {
    const auto dir = fresh_temp_dir();
    auto opts = make_options(dir); // dead host: success proves pure cache path
    const tile_key key{9, 5, 7};
    seed_cache(opts, 9, 5, 7, png_bytes());

    harness h(std::move(opts));
    h.src.request(tile_request{key, 5, 7});

    REQUIRE(h.pump_until([&] { return !h.payloads.empty(); }));
    CHECK(h.drops.empty());
    REQUIRE(h.payloads.size() == 1);
    const auto &p = h.payloads.front();
    CHECK(p.key == key);
    CHECK(p.albedo->width == 2);
    CHECK(p.height->height == 2);
    CHECK(p.normals->format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    fs::remove_all(dir);
}

TEST_CASE("http fetch writes through to the cache") {
    const auto dir = fresh_temp_dir();
    const test_server server;
    const tile_key key{10, 3, 4};

    {
        harness h(make_options(dir, server.host()));
        h.src.request(tile_request{key, 3, 4});
        REQUIRE(h.pump_until([&] { return !h.payloads.empty(); }));
        CHECK(h.drops.empty());
    }

    // all three assets landed in the cache...
    const auto opts = make_options(dir);
    const int zoom = 10, x = 3, z = 4;
    CHECK(fs::exists(std::vformat(opts.texture_cache_path, std::make_format_args(zoom, x, z))));
    CHECK(fs::exists(std::vformat(opts.heightmap_cache_path, std::make_format_args(zoom, x, z))));
    CHECK(fs::exists(std::vformat(opts.normals_cache_path, std::make_format_args(zoom, x, z))));

    // ...so a second source with a dead host still succeeds
    harness h2(make_options(dir));
    h2.src.request(tile_request{key, 3, 4});
    CHECK(h2.pump_until([&] { return !h2.payloads.empty(); }));
    fs::remove_all(dir);
}

TEST_CASE("corrupt cached bytes become a failure drop") {
    const auto dir = fresh_temp_dir();
    auto opts = make_options(dir);
    const tile_key key{9, 1, 1};
    seed_cache(opts, 9, 1, 1, "definitely not a png");

    harness h(std::move(opts));
    h.src.request(tile_request{key, 1, 1});

    REQUIRE(h.pump_until([&] { return !h.drops.empty(); }));
    CHECK(h.payloads.empty());
    REQUIRE(h.drops.size() == 1);
    CHECK(h.drops.front().key == key);
    CHECK_FALSE(h.drops.front().cancelled);
    CHECK_FALSE(h.drops.front().reason.empty());
    fs::remove_all(dir);
}

TEST_CASE("http error becomes a failure drop") {
    const auto dir = fresh_temp_dir();
    const test_server server;
    auto opts = make_options(dir, server.host());
    opts.texture_url_path = "/no-such-route/:zoom:/:x:/:y:.png"; // 404

    harness h(std::move(opts));
    h.src.request(tile_request{tile_key{9, 2, 2}, 2, 2});

    REQUIRE(h.pump_until([&] { return !h.drops.empty(); }));
    CHECK(h.payloads.empty());
    CHECK_FALSE(h.drops.front().cancelled);
    CHECK(h.drops.front().reason.find("404") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("requests dedup by key while a job is in flight") {
    const auto dir = fresh_temp_dir();
    const test_server server;
    auto opts = make_options(dir, server.host());
    opts.texture_url_path = "/slow/:zoom:/:x:/:y:.png"; // 2s busy window

    harness h(std::move(opts));
    const tile_key key{9, 6, 6};
    h.src.request(tile_request{key, 6, 6});
    std::this_thread::sleep_for(50ms); // ensure pickup
    h.src.request(tile_request{key, 6, 6}); // dedup: job is executing

    REQUIRE(h.pump_until([&] { return !h.payloads.empty(); }, 10000ms));
    // give a hypothetical duplicate time to appear, then check it didn't
    h.pump_until([&] { return h.payloads.size() > 1; }, 300ms);
    CHECK(h.payloads.size() == 1);
    CHECK(h.drops.empty());
    fs::remove_all(dir);
}

TEST_CASE("cancel before pickup yields a cancelled drop and no payload") {
    const auto dir = fresh_temp_dir();
    const test_server server;
    auto opts = make_options(dir, server.host());
    opts.texture_url_path = "/slow/:zoom:/:x:/:y:.png";

    harness h(std::move(opts));
    const tile_key busy{9, 8, 8};
    const tile_key victim{9, 9, 9};
    h.src.request(tile_request{busy, 8, 8}); // pins the single worker for ~2s
    std::this_thread::sleep_for(50ms); // ensure `busy` was picked up first
    h.src.request(tile_request{victim, 9, 9}); // queued behind
    h.src.cancel(victim); // still pending — flag set before pickup

    REQUIRE(h.pump_until([&] { return h.payloads.size() + h.drops.size() >= 2; }, 10000ms));
    REQUIRE(h.drops.size() == 1);
    CHECK(h.drops.front().key == victim);
    CHECK(h.drops.front().cancelled);
    CHECK(h.drops.front().reason.empty());
    REQUIRE(h.payloads.size() == 1);
    CHECK(h.payloads.front().key == busy);
    fs::remove_all(dir);
}
