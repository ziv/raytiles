/// Tests for tile_source (src/detail/tile_source.h).
///
/// No network is involved: the happy paths pre-populate the on-disk tile
/// cache so workers take the cache branch, and the failure paths use either
/// a corrupt cached PNG (decode error) or an unreachable localhost port
/// (connection refused — instant, no timeout wait). raylib's CPU-side image
/// functions (GenImageColor / ExportImage) are used to author valid PNGs;
/// they need no window or GL context.
#include "doctest.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <thread>

#include "raylib.h"
#include "raytiles/raytiles.h"
#include "detail/tile_source.h"
#include "detail/utils.hpp"

using raytiles::source_options;
using raytiles::tile_key;
using raytiles::tile_payload;
using raytiles::tile_source;
namespace fs = std::filesystem;

namespace {
    /// Fresh cache root per test case, wiped up front so stale files from an
    /// aborted run can't satisfy (or corrupt) a later one.
    fs::path cache_root(const std::string &name) {
        const auto root = fs::path("tile_source_test_cache") / name;
        fs::remove_all(root);
        fs::create_directories(root);
        return root;
    }

    /// Options pointing at `root` for all three caches. The host is an
    /// unreachable localhost port: cache hits never touch it, cache misses
    /// fail instantly with connection-refused.
    source_options make_options(const fs::path &root) {
        source_options opts;
        opts.download_threads = 2;
        opts.texture_cache_path = (root / "t" / "{}" / "{}" / "{}.png").string();
        opts.heightmap_cache_path = (root / "h" / "{}" / "{}" / "{}.png").string();
        opts.normals_cache_path = (root / "n" / "{}" / "{}" / "{}.png").string();
        opts.texture_host = "http://127.0.0.1:1";
        opts.heightmap_host = "http://127.0.0.1:1";
        opts.normals_host = "http://127.0.0.1:1";
        opts.texture_url_path = "/t/:zoom:/:x:/:y:.png";
        opts.heightmap_url_path = "/h/:zoom:/:x:/:y:.png";
        opts.normals_url_path = "/n/:zoom:/:x:/:y:.png";
        return opts;
    }

    /// Writes a valid 4x4 PNG of the given color at the vformat-expanded
    /// cache path for (zoom, x, z).
    void put_png(const std::string &path_template, const int zoom, const int x, const int z, const Color color) {
        const auto path = std::vformat(path_template, std::make_format_args(zoom, x, z));
        fs::create_directories(fs::path(path).parent_path());
        Image img = GenImageColor(4, 4, color);
        REQUIRE(ExportImage(img, path.c_str()));
        UnloadImage(img);
    }

    /// Seeds all three caches for one tile. The heightmap pixel encodes
    /// terrarium sea level (128,0,0) so decoding can be asserted end to end.
    void put_tile(const source_options &opts, const int zoom, const int x, const int z) {
        put_png(opts.texture_cache_path, zoom, x, z, Color{10, 20, 30, 255});
        put_png(opts.heightmap_cache_path, zoom, x, z, Color{128, 0, 0, 255});
        put_png(opts.normals_cache_path, zoom, x, z, Color{128, 128, 255, 255});
    }

    /// Polls `drain` until at least `n` payloads arrived or ~2s passed.
    std::vector<tile_payload> wait_for_payloads(tile_source &src, const std::size_t n) {
        std::vector<tile_payload> got;
        for (int i = 0; i < 200 && got.size() < n; ++i) {
            for (auto &&p: src.drain()) got.push_back(std::move(p));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return got;
    }

    /// Polls `drain_failures` until at least `n` keys arrived or ~2s passed.
    std::vector<tile_key> wait_for_failures(tile_source &src, const std::size_t n) {
        std::vector<tile_key> got;
        for (int i = 0; i < 200 && got.size() < n; ++i) {
            for (auto &&k: src.drain_failures()) got.push_back(k);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return got;
    }
} // namespace

TEST_CASE("tile_source: delivers a complete payload from the disk cache") {
    const auto root = cache_root("happy");
    const auto opts = make_options(root);
    put_tile(opts, 9, 306, 207);

    tile_source src(opts);
    const tile_key key{9, 0, 0}; // anchor-relative key, absolute coords below
    src.request(key, 306, 207);

    auto got = wait_for_payloads(src, 1);
    REQUIRE(got.size() == 1);
    CHECK(got[0].key == key);

    // all three images decoded, with their pixels owned by the payload
    REQUIRE(got[0].albedo.valid());
    REQUIRE(got[0].height.valid());
    REQUIRE(got[0].normals.valid());
    CHECK(got[0].albedo->width == 4);
    CHECK(got[0].albedo->height == 4);

    // the heightmap decodes as terrarium sea level end to end
    CHECK(raytiles::utils::get_height_from_image(*got[0].height, 0, 0) == doctest::Approx(0.0f));
}

TEST_CASE("tile_source: duplicate request while in flight is a no-op") {
    const auto root = cache_root("dedup");
    const auto opts = make_options(root);
    put_tile(opts, 9, 306, 207);

    tile_source src(opts);
    const tile_key key{9, 0, 0};
    src.request(key, 306, 207);
    src.request(key, 306, 207); // may or may not still be in flight — never crashes

    // at least one payload; a second is only possible if the first had
    // already delivered (request-after-completion is a fresh job by design)
    const auto got = wait_for_payloads(src, 1);
    CHECK(got.size() >= 1);
    for (const auto &p: got) CHECK(p.key == key);
}

TEST_CASE("tile_source: corrupt cached PNG reports the key as failed") {
    const auto root = cache_root("corrupt");
    const auto opts = make_options(root);
    put_tile(opts, 9, 306, 207);

    // overwrite the heightmap with garbage — decode must fail, no network
    const int zoom = 9, x = 306, z = 207;
    const auto hm = std::vformat(opts.heightmap_cache_path, std::make_format_args(zoom, x, z));
    std::ofstream(hm, std::ios::binary) << "not a png";

    tile_source src(opts);
    const tile_key key{9, 0, 0};
    src.request(key, 306, 207);

    const auto failed = wait_for_failures(src, 1);
    REQUIRE(failed.size() == 1);
    CHECK(failed[0] == key);
    CHECK(src.drain().empty());
}

TEST_CASE("tile_source: unreachable host reports the key as failed") {
    const auto root = cache_root("refused");
    const auto opts = make_options(root); // no cached files: forces the network path

    tile_source src(opts);
    const tile_key key{9, 3, 4};
    src.request(key, 309, 211);

    const auto failed = wait_for_failures(src, 1);
    REQUIRE(failed.size() == 1);
    CHECK(failed[0] == key);
}

TEST_CASE("tile_source: cancelling an unknown key is a safe no-op") {
    const auto root = cache_root("cancel-unknown");
    tile_source src(make_options(root));
    src.cancel(tile_key{9, 99, 99});
    CHECK(src.drain().empty());
    CHECK(src.drain_failures().empty());
}

TEST_CASE("tile_source: cancelled-before-pickup job never delivers") {
    const auto root = cache_root("cancel-queued");
    auto opts = make_options(root);
    // a single worker draining a queue of dead-host blockers ahead of the
    // victim: the main thread cancels the still-queued victim in
    // microseconds while the worker needs milliseconds to burn through the
    // blockers, so the cancel deterministically lands before pickup.
    opts.download_threads = 1;
    put_tile(opts, 9, 306, 207);

    tile_source src(opts);
    constexpr int blockers = 10;
    for (int i = 0; i < blockers; ++i) src.request(tile_key{9, 100 + i, 100}, 406 + i, 307);
    const tile_key victim{9, 0, 0}; // cached, queued behind the blockers
    src.request(victim, 306, 207);
    src.cancel(victim);

    // every blocker fails; the victim must surface in neither queue...
    const auto failed = wait_for_failures(src, blockers);
    REQUIRE(failed.size() == blockers);
    for (const auto &k: failed) CHECK(k != victim);
    CHECK(wait_for_payloads(src, 1).empty());

    // ...but a re-request revives it (fresh job, cache hit)
    src.request(victim, 306, 207);
    const auto got = wait_for_payloads(src, 1);
    REQUIRE(got.size() == 1);
    CHECK(got[0].key == victim);
}
