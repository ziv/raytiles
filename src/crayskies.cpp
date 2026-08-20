/// @file crayskies.cpp
/// Implementation of the C wrapper declared in crayskies.h. Translates C
/// structs / opaque handles into raytiles::sky::sky_steamer calls.
#include "../include/raytiles/crayskies.h"

#include <new>
#include <utility>

#include "../include/raytiles/rayskies.h"

// ---------------------------------------------------------------------------
//  Opaque handle type
// ---------------------------------------------------------------------------

struct RaytilesSkyStreamer {
  raytiles::sky::sky_steamer impl;

  explicit RaytilesSkyStreamer(const raytiles::sky::sky_config& config) : impl(config) {}
};

// ---------------------------------------------------------------------------
//  C -> C++ struct conversion helper
// ---------------------------------------------------------------------------

namespace {
raytiles::sky::sky_config to_cpp_sky(const RaytilesSkyConfig* c) {
  raytiles::sky::sky_config s{};
  if (!c) return s;
  for (int i = 0; i < 3; ++i) s.zenith_color[i] = c->zenith_color[i];
  for (int i = 0; i < 3; ++i) s.horizon_color[i] = c->horizon_color[i];
  return s;
}
}  // namespace

extern "C" {

// ---------------------------------------------------------------------------
//  Default-initializer
// ---------------------------------------------------------------------------

RaytilesSkyConfig RaytilesSkyConfigDefault(void) {
  constexpr raytiles::sky::sky_config s{};
  RaytilesSkyConfig out{};
  for (int i = 0; i < 3; ++i) out.zenith_color[i] = s.zenith_color[i];
  for (int i = 0; i < 3; ++i) out.horizon_color[i] = s.horizon_color[i];
  return out;
}

// ---------------------------------------------------------------------------
//  Sky streamer
// ---------------------------------------------------------------------------

RaytilesSkyStreamer* RaytilesSkyStreamerCreate(const RaytilesSkyConfig* config) {
  try {
    return new RaytilesSkyStreamer(to_cpp_sky(config));
  } catch (...) {
    return nullptr;
  }
}

void RaytilesSkyStreamerDestroy(RaytilesSkyStreamer* sky) { delete sky; }

void RaytilesSkyStreamerDraw(RaytilesSkyStreamer* sky, const Vector3 playerPos) {
  if (!sky) return;
  sky->impl.draw(playerPos);
}

// ---------------------------------------------------------------------------
//  Color setters
// ---------------------------------------------------------------------------

void RaytilesSkyStreamerSetHorizonColor(RaytilesSkyStreamer* sky, const Color color) {
  if (!sky) return;
  sky->impl.set_horizon_color(color);
}

void RaytilesSkyStreamerSetHorizonColorRGB(RaytilesSkyStreamer* sky, const float r, const float g, const float b) {
  if (!sky) return;
  sky->impl.set_horizon_color(r, g, b);
}

void RaytilesSkyStreamerSetZenithColor(RaytilesSkyStreamer* sky, const Color color) {
  if (!sky) return;
  sky->impl.set_zenith_color(color);
}

void RaytilesSkyStreamerSetZenithColorRGB(RaytilesSkyStreamer* sky, const float r, const float g, const float b) {
  if (!sky) return;
  sky->impl.set_zenith_color(r, g, b);
}

}  // extern "C"
