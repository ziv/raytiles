/// Tests for the pure terrain-synthesis helpers (src/detail/terrain_synth.hpp).
#include <vector>

#include "detail/terrain_synth.hpp"
#include "doctest.h"

using raytiles::synth::decode_terrarium_floats;
using raytiles::synth::default_normals_image;
using raytiles::synth::encode_terrarium;
using raytiles::synth::upsample_quadrant;

namespace {
// wrap encode_terrarium's malloc'd image so tests never leak
struct owned_image {
  Image img;
  ~owned_image() { std::free(img.data); }
};
}  // namespace

TEST_CASE("terrarium encode/decode round-trips at 1/256 m granularity") {
  const std::vector<float> heights = {
      0.0f,          8848.0f, -415.0f, 100.5f, 1234.25f,
      255.99609375f,  // 255 + 255/256: g/b carry boundary
      256.0f,         // the exact carry
      -0.00390625f,   // just below zero
  };
  const owned_image enc{encode_terrarium(heights, static_cast<int>(heights.size()), 1)};
  const auto decoded = decode_terrarium_floats(enc.img);

  REQUIRE(decoded.size() == heights.size());
  for (std::size_t i = 0; i < heights.size(); ++i) {
    CAPTURE(heights[i]);
    CHECK(decoded[i] == doctest::Approx(heights[i]).epsilon(0).scale(1).epsilon(1.0 / 256.0));
  }
}

TEST_CASE("terrarium encode is carry-safe around g-wrap boundaries") {
  // 255.998 m sits between 255+255/256 and 256; per-channel rounding would
  // produce r=0,g=255,b=255 vs r=1,g=0,b=0 inconsistently — fixed-point
  // encoding must pick one representable neighbor, exactly.
  const std::vector<float> heights = {255.998f};
  const owned_image enc{encode_terrarium(heights, 1, 1)};
  const auto* p = static_cast<const unsigned char*>(enc.img.data);
  const long fixed = (long(p[0]) << 16) + (long(p[1]) << 8) + long(p[2]);
  // (255.998 + 32768) * 256 = 8454143.5 → rounds to one of the two neighbors
  CHECK((fixed == 8454143L || fixed == 8454144L));
  const auto decoded = decode_terrarium_floats(enc.img);
  CHECK(decoded[0] == doctest::Approx(255.998f).epsilon(1.0 / 256.0));
}

TEST_CASE("upsample_quadrant reproduces a linear ramp in the interior") {
  // 8x8 ramp along x: h(x, y) = 16 * x
  constexpr int w = 8, h = 8;
  std::vector<float> src(w * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) src[y * w + x] = 16.0f * static_cast<float>(x);

  const auto q0 = upsample_quadrant(src, w, h, 0, 0);

  // dst texel i samples source coordinate i/2 - 0.25; interior values are
  // exactly linear, edges clamp
  CHECK(q0[0 * w + 2] == doctest::Approx(16.0f * 0.75f));  // sx = 0.75
  CHECK(q0[0 * w + 3] == doctest::Approx(16.0f * 1.25f));
  CHECK(q0[0 * w + 6] == doctest::Approx(16.0f * 2.75f));
  CHECK(q0[0 * w + 0] == doctest::Approx(0.0f));  // sx = -0.25 clamps to texel 0

  // constant along y: any row identical
  CHECK(q0[5 * w + 3] == doctest::Approx(q0[0 * w + 3]));
}

TEST_CASE("sibling quadrants are continuous across their shared edge") {
  constexpr int w = 8, h = 8;
  std::vector<float> src(w * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) src[y * w + x] = 16.0f * static_cast<float>(x);

  const auto q0 = upsample_quadrant(src, w, h, 0, 0);
  const auto q1 = upsample_quadrant(src, w, h, 1, 0);

  // q0's last column samples sx = 3.25, q1's first column samples sx = 3.75:
  // the two adjacent fine samples straddling the quadrant boundary — on a
  // ramp they differ by exactly half a source step
  CHECK(q0[0 * w + (w - 1)] == doctest::Approx(16.0f * 3.25f));
  CHECK(q1[0 * w + 0] == doctest::Approx(16.0f * 3.75f));
  CHECK(q1[0 * w + 0] - q0[0 * w + (w - 1)] == doctest::Approx(8.0f));
}

TEST_CASE("vertical quadrant selection mirrors the horizontal one") {
  constexpr int w = 4, h = 4;
  std::vector<float> src(w * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) src[y * w + x] = 100.0f * static_cast<float>(y);

  const auto top = upsample_quadrant(src, w, h, 0, 0);
  const auto bottom = upsample_quadrant(src, w, h, 0, 1);
  CHECK(top[0] == doctest::Approx(0.0f));                            // sy = -0.25 clamps
  CHECK(bottom[(h - 1) * w + 0] == doctest::Approx(100.0f * 3.0f));  // sy = 3.75 clamps toward texel 3
  CHECK(bottom[0 * w + 0] == doctest::Approx(100.0f * 1.75f));       // sy = 1.75
}

TEST_CASE("default normals are flat up-normals") {
  const owned_image img{default_normals_image(4)};
  CHECK(img.img.width == 4);
  CHECK(img.img.height == 4);
  CHECK(img.img.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8);
  const auto* p = static_cast<const unsigned char*>(img.img.data);
  for (int i = 0; i < 4 * 4; ++i) {
    CHECK(p[i * 3 + 0] == 128);
    CHECK(p[i * 3 + 1] == 128);
    CHECK(p[i * 3 + 2] == 255);
  }
}
