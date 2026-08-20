#pragma once
/// Pure terrain-synthesis helpers: derive higher-zoom Terrarium heightmaps
/// from their native-zoom ancestors, and produce default normal maps.
///
/// This header must stay pure — no raylib calls, no I/O — the same contract
/// as lod.hpp: workers call these functions, and the unit tests in
/// tests/terrain_synth_tests.cpp pin the math. raylib *types* (Image) are
/// fine; raylib *functions* are not. Pixel buffers are std::malloc'd, which
/// matches raylib's default RL_FREE, so UnloadImage (raii::image) is the
/// correct deleter for every Image produced here.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

#include "raytiles/raytiles.h"
#include "utils.hpp"

namespace raytiles::synth {
/// Decode a Terrarium RGB(A)8 image into float heights (meters). Preserves
/// the full b-channel fraction (unlike the uint16 query grid).
inline std::vector<float> decode_terrarium_floats(const Image& img) {
  std::vector<float> out(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height));
  for (int y = 0; y < img.height; ++y)
    for (int x = 0; x < img.width; ++x)
      out[static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(x)] = utils::get_height_from_image(img, x, y);
  return out;
}

/// Upsample one quadrant (qx, qz in {0, 1}) of a w×h height grid 2× to a full
/// w×h grid. Texel-center aligned: destination texel i samples source
/// coordinate `q·w/2 + (i + 0.5)/2 − 0.5`, so the two children sharing an
/// edge sample adjacent source positions straddling the boundary — derived
/// siblings are continuous by construction. Edges clamp to the source grid.
inline std::vector<float> upsample_quadrant(const std::span<const float> src, const int w, const int h, const int qx, const int qz) {
  std::vector<float> out(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));

  const auto sample = [&](int x, int y) {
    x = std::clamp(x, 0, w - 1);
    y = std::clamp(y, 0, h - 1);
    return src[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)];
  };

  for (int j = 0; j < h; ++j) {
    const float sy = static_cast<float>(qz) * static_cast<float>(h) / 2.0f + (static_cast<float>(j) + 0.5f) / 2.0f - 0.5f;
    const int y0 = static_cast<int>(std::floor(sy));
    const float ty = sy - static_cast<float>(y0);
    for (int i = 0; i < w; ++i) {
      const float sx = static_cast<float>(qx) * static_cast<float>(w) / 2.0f + (static_cast<float>(i) + 0.5f) / 2.0f - 0.5f;
      const int x0 = static_cast<int>(std::floor(sx));
      const float tx = sx - static_cast<float>(x0);

      const float top = std::lerp(sample(x0, y0), sample(x0 + 1, y0), tx);
      const float bottom = std::lerp(sample(x0, y0 + 1), sample(x0 + 1, y0 + 1), tx);
      out[static_cast<std::size_t>(j) * static_cast<std::size_t>(w) + static_cast<std::size_t>(i)] = std::lerp(top, bottom, ty);
    }
  }
  return out;
}

/// Encode float heights (meters) into a Terrarium RGB8 image. Carry-safe:
/// the height is quantized once into 24-bit fixed point ((h + 32768) · 256)
/// and split into bytes — never rounded per channel, which would spike at
/// g-wrap boundaries.
inline Image encode_terrarium(const std::span<const float> heights, const int w, const int h) {
  auto* pixels = static_cast<unsigned char*>(std::malloc(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3));
  for (std::size_t i = 0; i < heights.size(); ++i) {
    const auto fixed = std::clamp(std::llround((static_cast<double>(heights[i]) + 32768.0) * 256.0), 0LL, 0xFFFFFFLL);
    pixels[i * 3 + 0] = static_cast<unsigned char>(fixed >> 16);
    pixels[i * 3 + 1] = static_cast<unsigned char>((fixed >> 8) & 0xFF);
    pixels[i * 3 + 2] = static_cast<unsigned char>(fixed & 0xFF);
  }
  Image img{};
  img.data = pixels;
  img.width = w;
  img.height = h;
  img.mipmaps = 1;
  img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
  return img;
}

/// A flat default normal map: solid RGB(128, 128, 255), which the terrain
/// shader decodes to the up-normal (0, 0, 1). Used whenever a real normals
/// asset is unavailable (above the native zoom, 404, corrupt bytes, ...).
inline Image default_normals_image(const int size) {
  auto* pixels = static_cast<unsigned char*>(std::malloc(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 3));
  for (std::size_t i = 0; i < static_cast<std::size_t>(size) * static_cast<std::size_t>(size); ++i) {
    pixels[i * 3 + 0] = 128;
    pixels[i * 3 + 1] = 128;
    pixels[i * 3 + 2] = 255;
  }
  Image img{};
  img.data = pixels;
  img.width = size;
  img.height = size;
  img.mipmaps = 1;
  img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
  return img;
}
}  // namespace raytiles::synth
