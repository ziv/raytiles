/// @file crayskies.h
/// C wrapper for the raytiles sky public API (see rayskies.h).
///
/// Mirrors the C++ API 1:1 in C-compatible form:
///   - `raytiles::sky::sky_config`   -> `RaytilesSkyConfig`
///                                      (default: `RaytilesSkyConfigDefault()`)
///   - `raytiles::sky::sky_steamer`  -> opaque `RaytilesSkyStreamer*`
///
/// Color setters are exposed directly on the streamer handle as
/// `RaytilesSkyStreamerSet*`.
///
/// `Vector3` and `Color` are passed by value to keep the ABI C-compatible
/// (no C++ references in the public surface).
///
/// All functions that touch GPU state require a live raylib GL context
/// (`InitWindow` first), matching the C++ contract.
#ifndef RAYTILES_C_SKY_LIBRARY_H
#define RAYTILES_C_SKY_LIBRARY_H

#include "raylib.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
//  Configuration struct
// ---------------------------------------------------------------------------

/// Sky gradient parameters. Mirrors `raytiles::sky::sky_config`.
/// Colors are RGB triples in the 0..1 range.
typedef struct RaytilesSkyConfig {
    /// Color at the top of the sky dome (RGB, 0..1).
    float zenith_color[3];

    /// Color at the horizon (RGB, 0..1).
    float horizon_color[3];
} RaytilesSkyConfig;

// ---------------------------------------------------------------------------
//  Default-initializer
// ---------------------------------------------------------------------------

/// Returns a `RaytilesSkyConfig` populated with the same defaults as the
/// C++ `raytiles::sky::sky_config{}`.
RaytilesSkyConfig RaytilesSkyConfigDefault(void);

// ---------------------------------------------------------------------------
//  Sky streamer
// ---------------------------------------------------------------------------

/// Opaque sky-streamer handle. Allocated with `RaytilesSkyStreamerCreate`,
/// freed with `RaytilesSkyStreamerDestroy`.
typedef struct RaytilesSkyStreamer RaytilesSkyStreamer;

/// Creates a sky streamer. Requires a live raylib GL context (`InitWindow`
/// first). `config` may be NULL to use the C++ default (equivalent to passing
/// `RaytilesSkyConfigDefault()`). The struct is copied; the caller may free it
/// on return. Returns NULL on allocation failure.
RaytilesSkyStreamer *RaytilesSkyStreamerCreate(const RaytilesSkyConfig *config);

/// Destroys a sky streamer and releases all GPU / CPU resources. NULL-safe.
void RaytilesSkyStreamerDestroy(RaytilesSkyStreamer *sky);

/// Renders the sky dome centered on `playerPos`. Call between `BeginMode3D` /
/// `EndMode3D`. NULL-safe. Mirrors `sky_steamer::draw`.
void RaytilesSkyStreamerDraw(RaytilesSkyStreamer *sky, Vector3 playerPos);

// ---------------------------------------------------------------------------
//  Color setters
// ---------------------------------------------------------------------------

/// Sets the horizon color of the sky gradient.
/// Two variants mirror the C++ overloads: `Color` (8-bit per channel) and
/// explicit float RGB components (0..1).
void RaytilesSkyStreamerSetHorizonColor(RaytilesSkyStreamer *sky, Color color);
void RaytilesSkyStreamerSetHorizonColorRGB(RaytilesSkyStreamer *sky, float r, float g, float b);

/// Sets the zenith (top-of-dome) color of the sky gradient.
/// Two variants mirror the C++ overloads: `Color` (8-bit per channel) and
/// explicit float RGB components (0..1).
void RaytilesSkyStreamerSetZenithColor(RaytilesSkyStreamer *sky, Color color);
void RaytilesSkyStreamerSetZenithColorRGB(RaytilesSkyStreamer *sky, float r, float g, float b);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // RAYTILES_C_SKY_LIBRARY_H
