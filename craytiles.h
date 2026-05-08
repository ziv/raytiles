/// @file craytiles.h
/// C wrapper for the raytiles public API (see raytiles.h).
///
/// Mirrors the C++ API in C-compatible form:
///   - `raytiles::config`      -> `RaytilesConfig`      (default: `RaytilesConfigDefault()`)
///   - `raytiles::pool_config` -> `RaytilesPoolConfig`  (default: `RaytilesPoolConfigDefault()`)
///   - `raytiles::streamer`    -> opaque `RaytilesStreamer*`
///
/// String fields use `const char*`; the wrapper copies them into the underlying
/// C++ `std::string` on construction. NULL is treated as an empty string.
///
/// All functions are safe to call only after `InitWindow` (a live GL context is
/// required for resource creation), matching the C++ contract.
#ifndef RAYTILES_C_LIBRARY_H
#define RAYTILES_C_LIBRARY_H

#include <stdbool.h>

#include "raylib.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Tunable parameters for a streamer instance. Mirrors `raytiles::config`.
/// Use `RaytilesConfigDefault()` to obtain a zeroed-but-defaulted instance, then
/// override fields as needed.
typedef struct RaytilesConfig {
    int    base_zoom;
    int    max_zoom;
    float  base_zoom_tile_size;
    int    rendering_radius;
    float  skirt_size;
    float  height_scale;
    float  update_distance;
    float  update_height;
    double upload_budget_sec;
    int    max_uploads_per_frame;
    int    anchor_x_tile;
    int    anchor_z_tile;
    double near_plane;
    double far_plane;
    bool   use_mipmap;
    float  skirt_drop;
    float  fog_start;
    float  fog_end;
    bool   use_logger;
} RaytilesConfig;

/// Pool / provider parameters. Mirrors `raytiles::pool_config`.
/// String fields may be NULL to mean "empty"; otherwise they are copied.
typedef struct RaytilesPoolConfig {
    int         download_threads;
    bool        allow_insecure_tls;
    bool        use_logger;
    const char *texture_cache_path;
    const char *heightmap_cache_path;
    const char *host;
    const char *texture_url_path;
    const char *texture_host;
    const char *heightmap_url_path;
    const char *heightmap_host;
    const char *token;
} RaytilesPoolConfig;

/// Opaque streamer handle. Allocated with `RaytilesStreamerCreate`,
/// freed with `RaytilesStreamerDestroy`.
typedef struct RaytilesStreamer RaytilesStreamer;

/// Returns a config populated with the same defaults as `raytiles::config{}`.
RaytilesConfig     RaytilesConfigDefault(void);

/// Returns a pool config populated with the same defaults as
/// `raytiles::pool_config{}`. String fields point to static storage owned by
/// the library; do not free them.
RaytilesPoolConfig RaytilesPoolConfigDefault(void);

/// Creates a streamer. Requires a live raylib GL context (`InitWindow` first).
/// Returns NULL on failure.
RaytilesStreamer *RaytilesStreamerCreate(const RaytilesConfig     *conf,
                                         const RaytilesPoolConfig *pool_conf);

/// Destroys a streamer and releases all GPU / CPU resources. NULL-safe.
void RaytilesStreamerDestroy(const RaytilesStreamer *streamer);

/// Updates the desired tile set based on the camera and promotes finished
/// downloads to GPU. Cheap to call every frame.
void RaytilesStreamerUpdate(const RaytilesStreamer *streamer, const Camera3D &camera);

/// Renders all currently loaded tiles. Call between `BeginMode3D` / `EndMode3D`.
void RaytilesStreamerDraw(const RaytilesStreamer *streamer, const Camera3D &camera);

/// Draws the 2D debug HUD. Call after `EndMode3D`, before `EndDrawing`.
void RaytilesStreamerDebug(const RaytilesStreamer *streamer, const Camera3D &camera);

/// Draws 3D debug overlays. Call inside `BeginMode3D` / `EndMode3D`.
void RaytilesStreamerDebug3D(const RaytilesStreamer *streamer, const Camera3D &camera);

/// Sets the ambient light color used by the displacement shader.
void RaytilesStreamerSetAmbientLight(const RaytilesStreamer *streamer, Color color);

/// Sets the fog color used for distance attenuation.
void RaytilesStreamerSetFogColor(const RaytilesStreamer *streamer, Color color);

/// Samples the terrain altitude (world Y) under `position`.
/// On success, writes the altitude to `*out_height` and returns true.
/// Returns false if no loaded tile covers the queried XZ point; `*out_height`
/// is left untouched.
bool RaytilesStreamerGroundHeight(const RaytilesStreamer *streamer,
                                  Vector3 position,
                                  float  *out_height);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // RAYTILES_C_LIBRARY_H
