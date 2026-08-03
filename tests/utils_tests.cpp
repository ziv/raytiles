/// Tests for pure helpers: Terrarium heightmap decoding
/// (utils::get_height_from_image) and the tile_key hash.
/// Both operate on plain memory — no GL context or raylib runtime needed.
#include <array>
#include <unordered_set>
#include <vector>

#include "detail/tile.hpp"
#include "detail/utils.hpp"
#include "doctest.h"
#include "raytiles/raytiles.h"

using raytiles::tile_key;
namespace utils = raytiles::utils;

namespace {
/// Builds a raylib Image POD over caller-owned pixel memory (never passed
/// to raylib functions, so no UnloadImage is involved).
Image make_image(std::vector<unsigned char>& pixels, const int w, const int h, const int format) {
  Image img{};
  img.data = pixels.data();
  img.width = w;
  img.height = h;
  img.mipmaps = 1;
  img.format = format;
  return img;
}
}  // namespace

TEST_CASE("terrarium: sea level decodes to zero") {
  // terrarium encodes height = r*256 + g + b/256 - 32768, so (128,0,0) = 0m
  std::vector<unsigned char> px = {128, 0, 0};
  const Image img = make_image(px, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
  CHECK(utils::get_height_from_image(img, 0, 0) == doctest::Approx(0.0f));
}

TEST_CASE("terrarium: decodes RGB and RGBA identically") {
  // (129, 100, 128) = 33024 + 100 + 0.5 - 32768 = 356.5m
  std::vector<unsigned char> rgb = {129, 100, 128};
  std::vector<unsigned char> rgba = {129, 100, 128, 255};
  const Image img3 = make_image(rgb, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
  const Image img4 = make_image(rgba, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
  CHECK(utils::get_height_from_image(img3, 0, 0) == doctest::Approx(356.5f));
  CHECK(utils::get_height_from_image(img4, 0, 0) == doctest::Approx(356.5f));
}

TEST_CASE("terrarium: coordinates clamp to the image edges") {
  // 2x1 image: left pixel = 0m, right pixel = 256m
  std::vector<unsigned char> px = {128, 0, 0, /**/ 129, 0, 0};
  const Image img = make_image(px, 2, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
  CHECK(utils::get_height_from_image(img, -5, 0) == doctest::Approx(0.0f));   // clamps to x=0
  CHECK(utils::get_height_from_image(img, 7, 9) == doctest::Approx(256.0f));  // clamps to (1,0)
}

TEST_CASE("terrarium: unsupported pixel formats return 0") {
  std::vector<unsigned char> px = {200};
  const Image img = make_image(px, 1, 1, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);
  CHECK(utils::get_height_from_image(img, 0, 0) == doctest::Approx(0.0f));
}

TEST_CASE("tile_key: hash agrees with equality") {
  constexpr std::hash<tile_key> h{};
  CHECK(h(tile_key{9, 1, 2}) == h(tile_key{9, 1, 2}));

  // negative (anchor-relative) coordinates must be stable too
  CHECK(h(tile_key{12, -3, -7}) == h(tile_key{12, -3, -7}));
}

TEST_CASE("tile_key: no collisions across a realistic neighborhood") {
  // every (zoom, x, z) a default-config streamer can produce around an
  // anchor must hash uniquely — collisions would silently merge tiles.
  constexpr std::hash<tile_key> h{};
  std::unordered_set<std::size_t> seen;
  std::size_t inserted = 0;
  for (int zoom = 9; zoom <= 15; ++zoom)
    for (int x = -64; x <= 64; ++x)
      for (int z = -64; z <= 64; ++z) {
        seen.insert(h(tile_key{zoom, x, z}));
        ++inserted;
      }
  CHECK(seen.size() == inserted);
}
