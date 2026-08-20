/// Tests for tile_source (src/detail/tile_source.h): the real worker pool
/// driven against pre-seeded disk caches and a local httplib server. Fully
/// offline and windowless — only CPU-side raylib calls (image authoring).
#include "detail/terrain_synth.hpp"
#include "detail/tile_source.h"
#include "doctest.h"

#if defined(_WIN32)
#define NOGDI
#define NOUSER
#endif
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "httplib.h"

using namespace std::chrono_literals;
using raytiles::network_config;
using raytiles::tile_key;
using raytiles::tile_payload;
using raytiles::tile_request;
using raytiles::tile_source;

namespace {
namespace fs = std::filesystem;

// valid PNG bytes (2x2 RGBA), authored once via raylib's CPU-side exporter
const std::string& png_bytes() {
  static const std::string bytes = [] {
    const Image img = GenImageColor(2, 2, RED);
    int size = 0;
    unsigned char* data = ExportImageToMemory(img, ".png", &size);
    std::string out(reinterpret_cast<const char*>(data), static_cast<std::size_t>(size));
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

void write_file(const fs::path& path, const std::string& bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream f(path, std::ios::binary);
  f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// options rooted in `dir`, pointing at a host that refuses connections;
// tests that want HTTP override the URLs with a live local server
network_config make_options(const fs::path& dir, const std::string& host = "http://127.0.0.1:9") {
  network_config net;
  net.threads = 1;  // deterministic pickup order for the cancel/dedup tests
  net.connection_timeout_sec = 1;
  net.read_timeout_sec = 5;  // above the slow-route sleep so slow != timeout
  net.cache_dir = dir.string();
  net.texture_url = host + "/tex/:zoom:/:x:/:y:.png";
  net.heightmap_url = host + "/hm/:zoom:/:x:/:y:.png";
  net.normals_url = host + "/nl/:zoom:/:x:/:y:.png";
  return net;
}

// cache paths as tile_source derives them from cache_dir
fs::path cache_path(const network_config& net, const std::string& kind, const int zoom, const int x, const int z) {
  return fs::path(net.cache_dir) / kind / std::to_string(zoom) / std::to_string(x) / (std::to_string(z) + ".png");
}

// a size×size Terrarium gradient: h(x, y) = 16·x + y
std::vector<float> gradient_heights(const int size) {
  std::vector<float> heights(static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) heights[static_cast<std::size_t>(y) * size + x] = 16.0f * static_cast<float>(x) + static_cast<float>(y);
  return heights;
}

// PNG bytes of a Terrarium-encoded gradient (main-thread raylib exporter)
std::string terrarium_png_bytes(const int size) {
  const auto heights = gradient_heights(size);
  Image img = raytiles::synth::encode_terrarium(heights, size, size);
  int len = 0;
  unsigned char* data = ExportImageToMemory(img, ".png", &len);
  std::string out(reinterpret_cast<const char*>(data), static_cast<std::size_t>(len));
  MemFree(data);
  std::free(img.data);
  return out;
}

// the exact bytes the synthesis path must produce for a lineage walk from a
// gradient ancestor: repeated quadrant upsampling then Terrarium encoding
std::vector<unsigned char> expected_derived_pixels(const int size, const std::vector<std::pair<int, int>>& quadrants) {
  auto floats = gradient_heights(size);
  for (const auto& [qx, qz] : quadrants) floats = raytiles::synth::upsample_quadrant(floats, size, size, qx, qz);
  Image img = raytiles::synth::encode_terrarium(floats, size, size);
  const auto* p = static_cast<const unsigned char*>(img.data);
  std::vector<unsigned char> out(p, p + static_cast<std::size_t>(size) * size * 3);
  std::free(img.data);
  return out;
}

void seed_cache(const network_config& net, const int zoom, const int x, const int z, const std::string& bytes) {
  write_file(cache_path(net, "texture", zoom, x, z), bytes);
  write_file(cache_path(net, "heightmap", zoom, x, z), bytes);
  write_file(cache_path(net, "normals", zoom, x, z), bytes);
}

// drains the source until `pred` holds or the deadline passes
struct harness {
  tile_source src;
  std::vector<tile_payload> payloads;
  std::vector<tile_source::drop> drops;

  explicit harness(const network_config& net) : src(net) {}

  template <typename Pred>
  bool pump_until(Pred pred, const std::chrono::milliseconds timeout = 5000ms) {
    std::vector<tile_payload> ready;
    std::vector<tile_source::drop> dropped;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
      src.drain(ready, dropped);
      for (auto& p : ready) payloads.push_back(std::move(p));
      for (auto& d : dropped) drops.push_back(std::move(d));
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
    const auto serve = [](const httplib::Request&, httplib::Response& res) { res.set_content(png_bytes(), "image/png"); };
    svr.Get(R"(/tex/.*)", serve);
    svr.Get(R"(/hm/.*)", serve);
    svr.Get(R"(/nl/.*)", serve);
    svr.Get(R"(/slow/.*)", [&](const httplib::Request& req, httplib::Response& res) {
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
}  // namespace

TEST_CASE("cache hit delivers a full payload without any network") {
  const auto dir = fresh_temp_dir();
  auto opts = make_options(dir);  // dead host: success proves pure cache path
  const tile_key key{9, 5, 7};
  seed_cache(opts, 9, 5, 7, png_bytes());

  harness h(std::move(opts));
  h.src.request(tile_request{key, 5, 7});

  REQUIRE(h.pump_until([&] { return !h.payloads.empty(); }));
  CHECK(h.drops.empty());
  REQUIRE(h.payloads.size() == 1);
  const auto& p = h.payloads.front();
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
  CHECK(fs::exists(cache_path(opts, "texture", 10, 3, 4)));
  CHECK(fs::exists(cache_path(opts, "heightmap", 10, 3, 4)));
  CHECK(fs::exists(cache_path(opts, "normals", 10, 3, 4)));

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

TEST_CASE("z16 heightmap is derived from a seeded z15 ancestor") {
  const auto dir = fresh_temp_dir();
  auto opts = make_options(dir);  // dead host: everything must come from cache/synthesis
  constexpr int size = 16;

  // ancestor at native zoom 15, absolute coords (10, 20); its q(1,0) child
  // at z16 is (21, 40)
  write_file(cache_path(opts, "heightmap", 15, 10, 20), terrarium_png_bytes(size));
  write_file(cache_path(opts, "texture", 16, 21, 40), png_bytes());

  harness h(std::move(opts));
  const tile_key key{16, 21, 40};
  h.src.request(tile_request{key, 21, 40});

  REQUIRE(h.pump_until([&] { return !h.payloads.empty(); }));
  CHECK(h.drops.empty());

  // the payload's height image must be byte-identical to an independently
  // computed decode → upsample(q1,0) → encode of the seeded ancestor
  const auto& hm = *h.payloads.front().height;
  REQUIRE(hm.width == size);
  REQUIRE(hm.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8);
  const auto expected = expected_derived_pixels(size, {{1, 0}});
  CHECK(std::memcmp(hm.data, expected.data(), expected.size()) == 0);

  // the derived tile was cached, and the background task eventually
  // materializes all four z16 children of the ancestor
  const auto opts2 = make_options(dir);
  CHECK(fs::exists(cache_path(opts2, "heightmap", 16, 21, 40)));
  const auto all_siblings = [&] {
    return fs::exists(cache_path(opts2, "heightmap", 16, 20, 40)) && fs::exists(cache_path(opts2, "heightmap", 16, 21, 40)) &&
           fs::exists(cache_path(opts2, "heightmap", 16, 20, 41)) && fs::exists(cache_path(opts2, "heightmap", 16, 21, 41));
  };
  CHECK(h.pump_until(all_siblings));
  fs::remove_all(dir);
}

TEST_CASE("z17 heightmap derives through the z16 chain from a z15 ancestor") {
  const auto dir = fresh_temp_dir();
  auto opts = make_options(dir);
  constexpr int size = 16;

  // ancestor (3, 5) at z15; target z17 tile (14, 21):
  // level 16 quadrant = ((14>>1)&1, (21>>1)&1) = (1, 0)
  // level 17 quadrant = (14&1, 21&1) = (0, 1)
  write_file(cache_path(opts, "heightmap", 15, 3, 5), terrarium_png_bytes(size));
  write_file(cache_path(opts, "texture", 17, 14, 21), png_bytes());

  harness h(std::move(opts));
  h.src.request(tile_request{tile_key{17, 14, 21}, 14, 21});

  REQUIRE(h.pump_until([&] { return !h.payloads.empty(); }));
  CHECK(h.drops.empty());
  const auto expected = expected_derived_pixels(size, {{1, 0}, {0, 1}});
  CHECK(std::memcmp(h.payloads.front().height->data, expected.data(), expected.size()) == 0);

  // background generation fills the LINEAGE siblings only: the four z16
  // children of the ancestor, and the four z17 children of the lineage z16
  // node (7, 10) — never the whole subtree (which would explode at deep
  // zooms: ~21k tiles per parent at z22)
  const auto opts2 = make_options(dir);
  const auto lineage_set = [&] {
    for (int x = 6; x < 8; ++x)
      for (int z = 10; z < 12; ++z)
        if (!fs::exists(cache_path(opts2, "heightmap", 16, x, z))) return false;
    for (int x = 14; x < 16; ++x)
      for (int z = 20; z < 22; ++z)
        if (!fs::exists(cache_path(opts2, "heightmap", 17, x, z))) return false;
    return true;
  };
  CHECK(h.pump_until(lineage_set));

  // a z17 cousin outside the lineage stays on-demand
  CHECK_FALSE(fs::exists(cache_path(opts2, "heightmap", 17, 12, 20)));
  fs::remove_all(dir);
}

TEST_CASE("z22 heightmap derives through the full 7-level chain") {
  const auto dir = fresh_temp_dir();
  auto opts = make_options(dir);
  constexpr int size = 16;

  // ancestor (1, 1) at z15; target z22 tile (137, 141) — quadrants per level:
  // 16:(0,0) 17:(0,0) 18:(0,0) 19:(1,1) 20:(0,1) 21:(0,0) 22:(1,1)
  write_file(cache_path(opts, "heightmap", 15, 1, 1), terrarium_png_bytes(size));
  write_file(cache_path(opts, "texture", 22, 137, 141), png_bytes());

  harness h(std::move(opts));
  h.src.request(tile_request{tile_key{22, 137, 141}, 137, 141});

  REQUIRE(h.pump_until([&] { return !h.payloads.empty(); }));
  CHECK(h.drops.empty());
  const auto expected = expected_derived_pixels(size, {{0, 0}, {0, 0}, {0, 0}, {1, 1}, {0, 1}, {0, 0}, {1, 1}});
  CHECK(std::memcmp(h.payloads.front().height->data, expected.data(), expected.size()) == 0);

  // lineage backfill reaches the deepest level (siblings of the target under
  // its z21 lineage node (68, 70))
  const auto opts2 = make_options(dir);
  CHECK(h.pump_until([&] { return fs::exists(cache_path(opts2, "heightmap", 22, 136, 140)); }));
  fs::remove_all(dir);
}

TEST_CASE("corrupt z15 ancestor drops the derived tile with a reason") {
  const auto dir = fresh_temp_dir();
  auto opts = make_options(dir);
  write_file(cache_path(opts, "heightmap", 15, 7, 7), "definitely not a png");
  write_file(cache_path(opts, "texture", 16, 14, 14), png_bytes());

  harness h(std::move(opts));
  h.src.request(tile_request{tile_key{16, 14, 14}, 14, 14});

  REQUIRE(h.pump_until([&] { return !h.drops.empty(); }));
  CHECK(h.payloads.empty());
  CHECK_FALSE(h.drops.front().cancelled);
  CHECK_FALSE(h.drops.front().reason.empty());
  fs::remove_all(dir);
}

TEST_CASE("above the native zoom, normals default without any network") {
  const auto dir = fresh_temp_dir();
  auto opts = make_options(dir);  // dead host
  constexpr int size = 16;
  write_file(cache_path(opts, "heightmap", 15, 2, 2), terrarium_png_bytes(size));
  write_file(cache_path(opts, "texture", 16, 4, 4), png_bytes());

  harness h(std::move(opts));
  h.src.request(tile_request{tile_key{16, 4, 4}, 4, 4});

  REQUIRE(h.pump_until([&] { return !h.payloads.empty(); }));
  const auto* p = static_cast<const unsigned char*>(h.payloads.front().normals->data);
  CHECK(p[0] == 128);
  CHECK(p[1] == 128);
  CHECK(p[2] == 255);
  fs::remove_all(dir);
}

TEST_CASE("missing normals fall back to flat defaults instead of dropping") {
  const auto dir = fresh_temp_dir();
  const test_server server;
  auto opts = make_options(dir, server.host());
  opts.normals_url = server.host() + "/no-such-route/:zoom:/:x:/:y:.png";  // 404

  harness h(std::move(opts));
  const tile_key key{9, 4, 4};
  h.src.request(tile_request{key, 4, 4});

  REQUIRE(h.pump_until([&] { return !h.payloads.empty(); }));
  CHECK(h.drops.empty());
  const auto& nl = *h.payloads.front().normals;
  CHECK(nl.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8);
  const auto* p = static_cast<const unsigned char*>(nl.data);
  CHECK(p[0] == 128);
  CHECK(p[1] == 128);
  CHECK(p[2] == 255);
  fs::remove_all(dir);
}

TEST_CASE("corrupt cached normals fall back to flat defaults") {
  const auto dir = fresh_temp_dir();
  const test_server server;
  auto opts = make_options(dir, server.host());
  write_file(cache_path(opts, "normals", 9, 5, 5), "definitely not a png");

  harness h(std::move(opts));
  h.src.request(tile_request{tile_key{9, 5, 5}, 5, 5});

  REQUIRE(h.pump_until([&] { return !h.payloads.empty(); }));
  CHECK(h.drops.empty());
  const auto* p = static_cast<const unsigned char*>(h.payloads.front().normals->data);
  CHECK(p[2] == 255);
  fs::remove_all(dir);
}

TEST_CASE("missing heightmap still drops the tile") {
  const auto dir = fresh_temp_dir();
  const test_server server;
  auto opts = make_options(dir, server.host());
  opts.heightmap_url = server.host() + "/no-such-route/:zoom:/:x:/:y:.png";  // 404

  harness h(std::move(opts));
  h.src.request(tile_request{tile_key{9, 6, 7}, 6, 7});

  REQUIRE(h.pump_until([&] { return !h.drops.empty(); }));
  CHECK(h.payloads.empty());
  CHECK_FALSE(h.drops.front().cancelled);
  fs::remove_all(dir);
}

TEST_CASE("http error becomes a failure drop") {
  const auto dir = fresh_temp_dir();
  const test_server server;
  auto opts = make_options(dir, server.host());
  opts.texture_url = server.host() + "/no-such-route/:zoom:/:x:/:y:.png";  // 404

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
  opts.texture_url = server.host() + "/slow/:zoom:/:x:/:y:.png";  // 2s busy window

  harness h(std::move(opts));
  const tile_key key{9, 6, 6};
  h.src.request(tile_request{key, 6, 6});
  std::this_thread::sleep_for(50ms);       // ensure pickup
  h.src.request(tile_request{key, 6, 6});  // dedup: job is executing

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
  opts.texture_url = server.host() + "/slow/:zoom:/:x:/:y:.png";

  harness h(std::move(opts));
  const tile_key busy{9, 8, 8};
  const tile_key victim{9, 9, 9};
  h.src.request(tile_request{busy, 8, 8});    // pins the single worker for ~2s
  std::this_thread::sleep_for(50ms);          // ensure `busy` was picked up first
  h.src.request(tile_request{victim, 9, 9});  // queued behind
  h.src.cancel(victim);                       // still pending — flag set before pickup

  REQUIRE(h.pump_until([&] { return h.payloads.size() + h.drops.size() >= 2; }, 10000ms));
  REQUIRE(h.drops.size() == 1);
  CHECK(h.drops.front().key == victim);
  CHECK(h.drops.front().cancelled);
  CHECK(h.drops.front().reason.empty());
  REQUIRE(h.payloads.size() == 1);
  CHECK(h.payloads.front().key == busy);
  fs::remove_all(dir);
}
