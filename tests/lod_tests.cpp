/// Tests for the pure LOD policy (src/detail/lod.hpp).
///
/// `lod::desired_tiles` is a deterministic function of (options, position),
/// so these tests need no window, network, or GL context. They lock in the
/// behavior of the historical `tiles_manager::build_required` implementation
/// that the module was extracted from:
///   - structural invariants (no ancestor/descendant overlap, disc bounds),
///   - qualitative behavior (max-zoom under a low camera, horizon cutoff),
///   - exact regression snapshots for the default configuration.
#include "doctest.h"

#include <unordered_set>

#include "detail/lod.hpp"

using raytiles::tile_key;
using raytiles::Zoom;
namespace lod = raytiles::lod;

namespace {
    std::unordered_set<tile_key> desired(const lod::options &o, const Vector3 pos) {
        std::unordered_set<tile_key> out;
        lod::desired_tiles(o, pos, out);
        return out;
    }

    /// The ancestor of `key` at `zoom` (arithmetic shift = floor division,
    /// correct for negative tile coordinates too).
    tile_key ancestor_at(const tile_key &key, const Zoom zoom) {
        const int shift = key.zoom - zoom;
        return {zoom, key.x >> shift, key.z >> shift};
    }

    /// Count of keys per zoom, for readable snapshot assertions.
    int count_at_zoom(const std::unordered_set<tile_key> &keys, const Zoom zoom) {
        int n = 0;
        for (const auto &k: keys) n += (k.zoom == zoom) ? 1 : 0;
        return n;
    }
} // namespace

TEST_CASE("lod: deterministic for identical inputs") {
    const lod::options o{};
    const Vector3 pos{33200.0f, 1000.0f, 33200.0f};
    CHECK(desired(o, pos) == desired(o, pos));
}

TEST_CASE("lod: tile under a low camera is desired at max zoom") {
    const lod::options o{};
    // camera hovering 1000m over the center of base tile (0,0).
    // at max zoom (15) a tile is base_tile_size/2^6 = 1037.5m wide, so the
    // camera at x=z=33200 stands over tile floor(33200/1037.5) = (32,32).
    const auto keys = desired(o, {33200.0f, 1000.0f, 33200.0f});

    CHECK(keys.contains(tile_key{15, 32, 32}));
    // ...and therefore no coarser ancestor of it may be desired
    CHECK(!keys.contains(tile_key{9, 0, 0}));
}

TEST_CASE("lod: output never contains a tile together with an ancestor") {
    const lod::options o{};
    const Vector3 positions[] = {
        {33200.0f, 1000.0f, 33200.0f},
        {33200.0f, 5000.0f, 33200.0f},
        {0.0f, 2000.0f, 0.0f}, // tile corner: negative coordinates in play
        {-50000.0f, 300.0f, 120000.0f},
    };
    for (const auto &pos: positions) {
        const auto keys = desired(o, pos);
        REQUIRE(!keys.empty());
        for (const auto &key: keys)
            for (Zoom z = o.base_zoom; z < key.zoom; ++z)
                CHECK(!keys.contains(ancestor_at(key, z)));
    }
}

TEST_CASE("lod: every tile maps back into the scan disc") {
    const lod::options o{};
    const Vector3 pos{33200.0f, 2000.0f, 33200.0f};
    const int cx = 0, cz = 0; // base tile under the camera
    const int allowed = (o.radius - 1) * (o.radius - 1);

    for (const auto keys = desired(o, pos); const auto &key: keys) {
        const auto [zoom, x, z] = ancestor_at(key, o.base_zoom);
        const int dx = x - cx;
        const int dz = z - cz;
        CHECK(dx * dx + dz * dz < allowed);
    }
}

TEST_CASE("lod: horizon cutoff keeps a ground camera's set small") {
    const lod::options o{};
    // at y=100 the horizon is ~35.7km: base-zoom tiles (>=100km away by
    // threshold) can never be desired, only close-in fine tiles survive.
    const auto ground = desired(o, {33200.0f, 100.0f, 33200.0f});
    CHECK(count_at_zoom(ground, 9) == 0);
    CHECK(count_at_zoom(ground, 10) == 0);
    CHECK(count_at_zoom(ground, 11) == 0);
    CHECK(count_at_zoom(ground, 15) > 0);
}

TEST_CASE("lod: a high camera stops subdividing before max zoom") {
    const lod::options o{};
    // at y=5000 every tile center is at least 5000m away (height is part of
    // the LOD distance), which is >= the zoom-14 threshold — so recursion
    // never reaches zoom 15.
    const auto high = desired(o, {33200.0f, 5000.0f, 33200.0f});
    CHECK(count_at_zoom(high, 15) == 0);
    CHECK(count_at_zoom(high, 9) > 0);
}

TEST_CASE("lod: regression snapshots for the default configuration") {
    // Exact desired-set sizes for fixed cameras, produced by the extracted
    // implementation. If a change here is *intended*, re-derive the numbers;
    // an unintended diff means the LOD behavior drifted.
    const lod::options o{};

    SUBCASE("low camera over tile center") {
        const auto keys = desired(o, {33200.0f, 1000.0f, 33200.0f});
        CHECK(keys.size() == 272);
        CHECK(count_at_zoom(keys, 15) == 64);
    }
    SUBCASE("high camera over tile center") {
        const auto keys = desired(o, {33200.0f, 5000.0f, 33200.0f});
        CHECK(keys.size() == 252);
        CHECK(count_at_zoom(keys, 9) == 36);
    }
    SUBCASE("camera above a 4-tile corner") {
        const auto keys = desired(o, {0.0f, 2000.0f, 0.0f});
        CHECK(keys.size() == 268);
        // documented zoom-skip: distance bands can jump 9 -> 11 directly
        CHECK(count_at_zoom(keys, 9) == 12);
        CHECK(count_at_zoom(keys, 10) == 0);
        CHECK(count_at_zoom(keys, 11) == 48);
    }
    SUBCASE("ground-level camera") {
        const auto keys = desired(o, {33200.0f, 100.0f, 33200.0f});
        CHECK(keys.size() == 204);
    }
}
