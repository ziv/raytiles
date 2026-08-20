/// Tests for src/detail/utils.hpp helpers and the tile_key hash.
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "detail/lod.hpp"  // pulls raytiles.h + tile.hpp + utils.hpp in the right order
#include "doctest.h"

using raytiles::tile_key;

namespace {
// Terrarium encoding: value = height + 32768; r = value / 256, g = value % 256,
// b = 256 * frac(value). Inverse of utils::get_height_from_image's decode.
struct rgb {
  unsigned char r, g, b;
};

rgb encode_terrarium(const float height) {
  const float value = height + 32768.0f;
  const auto whole = static_cast<int>(value);
  return rgb{
      static_cast<unsigned char>(whole / 256),
      static_cast<unsigned char>(whole % 256),
      static_cast<unsigned char>((value - static_cast<float>(whole)) * 256.0f),
  };
}

// Minimal Image builder over a caller-owned pixel buffer (no raylib calls,
// nothing to unload).
Image make_image(std::vector<unsigned char>& pixels, const int width, const int height, const int format) {
  Image img{};
  img.data = pixels.data();
  img.width = width;
  img.height = height;
  img.mipmaps = 1;
  img.format = format;
  return img;
}
}  // namespace

TEST_CASE("terrarium decode round-trips known heights (RGB)") {
  const float heights[] = {0.0f, 8848.0f, -415.0f, 100.5f, 1234.25f};

  std::vector<unsigned char> pixels(5 * 1 * 3);
  for (int i = 0; i < 5; ++i) {
    const auto [r, g, b] = encode_terrarium(heights[i]);
    pixels[i * 3 + 0] = r;
    pixels[i * 3 + 1] = g;
    pixels[i * 3 + 2] = b;
  }
  const Image img = make_image(pixels, 5, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8);

  for (int i = 0; i < 5; ++i) {
    CAPTURE(heights[i]);
    CHECK(raytiles::utils::get_height_from_image(img, i, 0) == doctest::Approx(heights[i]).epsilon(0.001));
  }
}

TEST_CASE("terrarium decode reads the RGB channels of RGBA images") {
  const float height = 2962.0f;  // Zugspitze
  const auto [r, g, b] = encode_terrarium(height);

  std::vector<unsigned char> pixels = {r, g, b, 255};
  const Image img = make_image(pixels, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

  CHECK(raytiles::utils::get_height_from_image(img, 0, 0) == doctest::Approx(height).epsilon(0.001));
}

TEST_CASE("out-of-range pixel coordinates clamp to the border") {
  // 2x2 image: distinct heights per texel
  const float h00 = 100.0f, h10 = 200.0f, h01 = 300.0f, h11 = 400.0f;
  std::vector<unsigned char> pixels(2 * 2 * 3);
  const float hs[] = {h00, h10, h01, h11};
  for (int i = 0; i < 4; ++i) {
    const auto [r, g, b] = encode_terrarium(hs[i]);
    pixels[i * 3 + 0] = r;
    pixels[i * 3 + 1] = g;
    pixels[i * 3 + 2] = b;
  }
  const Image img = make_image(pixels, 2, 2, PIXELFORMAT_UNCOMPRESSED_R8G8B8);

  CHECK(raytiles::utils::get_height_from_image(img, -5, 0) == doctest::Approx(h00));
  CHECK(raytiles::utils::get_height_from_image(img, 7, 0) == doctest::Approx(h10));
  CHECK(raytiles::utils::get_height_from_image(img, 0, -1) == doctest::Approx(h00));
  CHECK(raytiles::utils::get_height_from_image(img, 1, 9) == doctest::Approx(h11));
}

TEST_CASE("unsupported pixel formats return 0") {
  std::vector<unsigned char> pixels = {128};
  const Image img = make_image(pixels, 1, 1, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);

  CHECK(raytiles::utils::get_height_from_image(img, 0, 0) == 0.0f);
}

TEST_CASE("build_height_grid encodes rounded meters offset by 32768") {
  const float heights[] = {0.0f, 8848.0f, -415.0f, 100.5f};
  std::vector<unsigned char> pixels(4 * 1 * 3);
  for (int i = 0; i < 4; ++i) {
    const auto [r, g, b] = encode_terrarium(heights[i]);
    pixels[i * 3 + 0] = r;
    pixels[i * 3 + 1] = g;
    pixels[i * 3 + 2] = b;
  }
  const Image img = make_image(pixels, 4, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8);

  const auto grid = raytiles::utils::build_height_grid(img);
  REQUIRE(grid.width == 4);
  REQUIRE(grid.height == 1);
  REQUIRE(grid.samples.size() == 4);
  CHECK(grid.samples[0] == 32768);  // 0 m
  CHECK(grid.samples[1] == 32768 + 8848);
  CHECK(grid.samples[2] == 32768 - 415);
  const bool half_rounded_to_neighbor = grid.samples[3] == 32768 + 100 || grid.samples[3] == 32768 + 101;
  CHECK(half_rounded_to_neighbor);  // .5 rounds either way per lround
}

TEST_CASE("sample_height_grid: texel centers, midpoints, and edge clamping") {
  // 2x1 grid: 100 m and 300 m
  raytiles::height_grid grid{2, 1, {32768 + 100, 32768 + 300}};

  // texel centers: u = 0.25 and 0.75
  CHECK(raytiles::utils::sample_height_grid(grid, 0.25f, 0.5f) == doctest::Approx(100.0f));
  CHECK(raytiles::utils::sample_height_grid(grid, 0.75f, 0.5f) == doctest::Approx(300.0f));

  // midpoint between the two texels: average
  CHECK(raytiles::utils::sample_height_grid(grid, 0.5f, 0.5f) == doctest::Approx(200.0f));

  // edges clamp to the border texels
  CHECK(raytiles::utils::sample_height_grid(grid, 0.0f, 0.0f) == doctest::Approx(100.0f));
  CHECK(raytiles::utils::sample_height_grid(grid, 1.0f, 1.0f) == doctest::Approx(300.0f));

  // vertical interpolation on a 1x2 grid
  raytiles::height_grid vgrid{1, 2, {32768 + 0, 32768 + 1000}};
  CHECK(raytiles::utils::sample_height_grid(vgrid, 0.5f, 0.5f) == doctest::Approx(500.0f));
}

TEST_CASE("tile_key hash: deterministic, equality-consistent, collision-free on a working-set grid") {
  constexpr std::hash<tile_key> hasher{};

  const tile_key a{12, 306, -207};
  CHECK(hasher(a) == hasher(tile_key{12, 306, -207}));

  // no 64-bit collisions across a superset of any realistic working set
  // (7 zooms x 64x64 grid centered on the anchor, negative coords included)
  std::unordered_set<std::uint64_t> seen;
  int total = 0;
  for (int zoom = 9; zoom <= 15; ++zoom)
    for (int x = -32; x < 32; ++x)
      for (int z = -32; z < 32; ++z) {
        seen.insert(hasher(tile_key{zoom, x, z}));
        ++total;
      }
  CHECK(static_cast<int>(seen.size()) == total);
}
