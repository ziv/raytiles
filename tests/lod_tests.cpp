/// Tests for the pure LOD policy (src/detail/lod.hpp).
///
/// The equivalence suite compares lod::desired_tiles against a *verbatim copy*
/// of tiles_manager::process_current_location + build_required as of commit
/// 141da96 — the revision the extraction started from. If the extracted policy
/// ever diverges from that algorithm, these tests fail. Note the reference
/// shares utils::distance_sq_to_tile / calculate_horizon with the policy, so
/// changes to those helpers are covered by the snapshot suite, not this one.
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "detail/lod.hpp"
#include "doctest.h"

using raytiles::tile_key;
using raytiles::Zoom;

namespace {
// --- reference implementation (verbatim behavior copy, see file header) ---
struct ref_tile_meta {
  float size;
  raytiles::MetersDSq threshold;
};

struct ref_impl {
  raytiles::lod::options opts;
  std::unordered_map<Zoom, ref_tile_meta> tiles;
  std::unordered_set<tile_key> desired_keys;

  explicit ref_impl(const raytiles::lod::options& o) : opts(o) {
    for (int zoom = opts.base_zoom; zoom <= opts.max_zoom; ++zoom) {
      const auto idx = static_cast<std::size_t>(zoom - opts.base_zoom);
      const auto ratio = static_cast<float>(1 << (zoom - opts.base_zoom));
      const auto size = opts.base_tile_size / ratio;
      // deliberate deviation from the verbatim copy: squared in double, in
      // lockstep with lod.hpp (float multiply could overflow — CodeQL).
      // bit-identical for any threshold whose square is float-exact, which
      // covers every default and test value; the snapshots prove it.
      const auto th = static_cast<raytiles::MetersDSq>(opts.thresholds[idx]);
      tiles[zoom] = ref_tile_meta{size, th * th};
    }
  }

  void build_required(const Vector3& position, const Zoom zoom, const int tx, const int tz, const float render_radius_sq) {
    if (zoom == opts.max_zoom) {
      desired_keys.insert({zoom, tx, tz});
      return;
    }

    const auto tile = &tiles[zoom];
    const raytiles::MetersDSq distance_sq = raytiles::utils::distance_sq_to_tile(position, {zoom, tx, tz}, tile->size);

    if (distance_sq > render_radius_sq) {
      return;
    }

    if (distance_sq >= tile->threshold) {
      desired_keys.insert({zoom, tx, tz});
      return;
    }

    const int child_zoom = zoom + 1;
    const int cx0 = tx * 2;
    const int cz0 = tz * 2;
    for (int ox = 0; ox < 2; ++ox)
      for (int oz = 0; oz < 2; ++oz) build_required(position, child_zoom, cx0 + ox, cz0 + oz, render_radius_sq);
  }

  std::unordered_set<tile_key> run(const Vector3& position) {
    desired_keys.clear();
    const int current_tile_x = static_cast<int>(std::floor(position.x / opts.base_tile_size));
    const int current_tile_z = static_cast<int>(std::floor(position.z / opts.base_tile_size));

    const auto r = opts.rendering_radius;
    const auto allowed_radius = (r - 1) * (r - 1);
    const auto render_radius_sq = static_cast<float>(raytiles::utils::calculate_horizon(position));

    for (int dx = -r; dx <= r; ++dx)
      for (int dz = -r; dz <= r; ++dz)
        if (dz * dz + dx * dx < allowed_radius) build_required(position, opts.base_zoom, current_tile_x + dx, current_tile_z + dz, render_radius_sq);

    return desired_keys;
  }
};

std::vector<tile_key> run_lod(const raytiles::lod::options& opts, const Vector3& position) {
  std::vector<tile_key> out;
  raytiles::lod::desired_tiles(opts, position, out);
  return out;
}

// positions exercised by the equivalence and invariant suites: tile
// centers, tile corners, off-grid fractions, negative coords — each at
// ground-skimming through high-altitude camera heights
std::vector<Vector3> probe_positions() {
  const float ts = 66400.0f;
  const std::vector<std::pair<float, float>> xz = {
      {0.0f, 0.0f},           {0.5f * ts, 0.5f * ts},    {0.25f * ts, 0.75f * ts}, {3.3f * ts, -2.7f * ts}, {-1.0f * ts, -1.0f * ts},
      {7.9f * ts, 0.1f * ts}, {-5.55f * ts, 4.05f * ts},
  };
  const std::vector<float> altitudes = {2.0f, 500.0f, 5000.0f, 60000.0f};

  std::vector<Vector3> out;
  for (const auto& [x, z] : xz)
    for (const float y : altitudes) out.push_back(Vector3{x, y, z});
  return out;
}

bool has_ancestor_in(const std::unordered_set<tile_key>& set, const tile_key& key, const int base_zoom) {
  int x = key.x, z = key.z;
  for (int zoom = key.zoom - 1; zoom >= base_zoom; --zoom) {
    x >>= 1;
    z >>= 1;
    if (set.contains(tile_key{zoom, x, z})) return true;
  }
  return false;
}
}  // namespace

TEST_CASE("lod::desired_tiles matches the pre-extraction algorithm") {
  raytiles::lod::options opts{};  // library defaults

  ref_impl ref(opts);
  for (const auto& pos : probe_positions()) {
    CAPTURE(pos.x);
    CAPTURE(pos.y);
    CAPTURE(pos.z);

    const auto expected = ref.run(pos);
    const auto actual = run_lod(opts, pos);

    REQUIRE(actual.size() == expected.size());
    for (const auto& key : actual) {
      CAPTURE(key.zoom);
      CAPTURE(key.x);
      CAPTURE(key.z);
      CHECK(expected.contains(key));
    }
  }
}

TEST_CASE("lod::desired_tiles matches the reference on non-default options") {
  raytiles::lod::options opts{};
  opts.base_zoom = 11;
  opts.max_zoom = 14;
  opts.base_tile_size = 16600.0f;
  opts.rendering_radius = 4;
  opts.thresholds = {40000.0f, 20000.0f, 10000.0f, 5000.0f, 2500.0f, 0.0f, 0.0f};

  ref_impl ref(opts);
  for (const auto& pos : probe_positions()) {
    CAPTURE(pos.x);
    CAPTURE(pos.y);
    CAPTURE(pos.z);

    const auto expected = ref.run(pos);
    const auto actual = run_lod(opts, pos);

    REQUIRE(actual.size() == expected.size());
    for (const auto& key : actual) CHECK(expected.contains(key));
  }
}

TEST_CASE("lod::desired_tiles matches the reference above the old z15 ceiling") {
  raytiles::lod::options opts{};
  opts.base_zoom = 9;
  opts.max_zoom = 17;
  opts.base_tile_size = 66400.0f;
  opts.rendering_radius = 4;
  opts.thresholds = {100000.0f, 80000.0f, 40000.0f, 20000.0f, 10000.0f, 5000.0f, 2500.0f, 1250.0f, 625.0f};

  ref_impl ref(opts);
  for (const auto& pos : probe_positions()) {
    CAPTURE(pos.x);
    CAPTURE(pos.y);
    CAPTURE(pos.z);

    const auto expected = ref.run(pos);
    const auto actual = run_lod(opts, pos);

    REQUIRE(actual.size() == expected.size());
    for (const auto& key : actual) CHECK(expected.contains(key));
  }

  // a low camera over a base-tile center reaches z16+ (a corner probe would
  // not: the horizon cap measures to tile centers, a pre-existing quirk the
  // equivalence suite pins)
  const auto low = run_lod(opts, Vector3{33200.0f, 500.0f, 33200.0f});
  const bool has_beyond_15 = std::ranges::any_of(low, [](const tile_key& k) { return k.zoom > 15; });
  CHECK(has_beyond_15);
}

TEST_CASE("desired sets hold structural invariants") {
  const raytiles::lod::options opts{};

  for (const auto& pos : probe_positions()) {
    CAPTURE(pos.x);
    CAPTURE(pos.y);
    CAPTURE(pos.z);

    const auto keys = run_lod(opts, pos);
    const std::unordered_set<tile_key> set(keys.begin(), keys.end());

    // duplicate-free
    CHECK(set.size() == keys.size());

    const int r = opts.rendering_radius;
    const int cx = static_cast<int>(std::floor(pos.x / opts.base_tile_size));
    const int cz = static_cast<int>(std::floor(pos.z / opts.base_tile_size));

    for (const auto& key : keys) {
      CAPTURE(key.zoom);
      CAPTURE(key.x);
      CAPTURE(key.z);

      // zoom bounds
      CHECK(key.zoom >= opts.base_zoom);
      CHECK(key.zoom <= opts.max_zoom);

      // no key together with an ancestor (a tile is never desired twice
      // at two zoom levels)
      CHECK_FALSE(has_ancestor_in(set, key, opts.base_zoom));

      // every key descends from a base tile inside the scanned disc
      int bx = key.x, bz = key.z;
      for (int zoom = key.zoom; zoom > opts.base_zoom; --zoom) {
        bx >>= 1;
        bz >>= 1;
      }
      const int dx = bx - cx;
      const int dz = bz - cz;
      CHECK(dx * dx + dz * dz < (r - 1) * (r - 1));
    }
  }
}

// Exact regression pins for the default options. Derived from the reference
// implementation (one-shot printer, see docs/refactor/step-01-lod-extraction.md);
// positions sit away from threshold boundaries so float ordering is stable
// across platforms.
TEST_CASE("desired-set snapshots") {
  const raytiles::lod::options opts{};

  const auto per_zoom = [](const std::vector<tile_key>& keys) {
    std::unordered_map<Zoom, int> counts;
    for (const auto& k : keys) ++counts[k.zoom];
    return counts;
  };

  SUBCASE("origin, low altitude") {
    const auto keys = run_lod(opts, Vector3{0.0f, 500.0f, 0.0f});
    const std::unordered_set<tile_key> set(keys.begin(), keys.end());
    auto counts = per_zoom(keys);

    CHECK(keys.size() == 252);
    CHECK(counts[9] == 0);
    CHECK(counts[15] == 64);
    CHECK(set.contains(tile_key{15, 0, 0}));
    CHECK(set.contains(tile_key{15, -1, -1}));
  }

  SUBCASE("mid-tile, cruise altitude") {
    const auto keys = run_lod(opts, Vector3{0.5f * 66400.0f, 5000.0f, 0.5f * 66400.0f});
    auto counts = per_zoom(keys);

    CHECK(keys.size() == 252);
    CHECK(counts[9] == 36);
    CHECK(counts[15] == 0);  // y^2 term keeps max zoom out at 5 km altitude
  }

  SUBCASE("high altitude collapses to coarse zooms") {
    const auto keys = run_lod(opts, Vector3{0.0f, 60000.0f, 0.0f});
    auto counts = per_zoom(keys);

    CHECK(keys.size() == 117);
    CHECK(counts[11] == 48);  // camera-height distance term caps refinement at z11
    CHECK(counts[15] == 0);
  }
}
