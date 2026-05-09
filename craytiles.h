/// @file craytiles.h
/// C wrapper for the raytiles public API (see raytiles.h).
///
/// Mirrors the C++ API in C-compatible form:
///   - `raytiles::config`      -> `RaytilesConfig`      (default: `RaytilesConfigDefault()`)
///   - `raytiles::pool_config` -> `RaytilesPoolConfig`  (default: `RaytilesPoolConfigDefault()`)
///   - `raytiles::streamer`    -> opaque `RaytilesStreamer*`
///
/// String fields in `RaytilesPoolConfig` are `const char*`; the wrapper copies
/// them into the underlying C++ `std::string` on construction. NULL is treated
/// as an empty string. Pointers passed to `RaytilesStreamerCreate` only need to
/// remain valid for the duration of that call.
///
/// `Camera3D`, `Vector3` and `Color` are passed by value to keep the ABI
/// C-compatible (no C++ references in the public surface).
///
/// All functions require a live raylib GL context (call `InitWindow` first),
/// matching the C++ contract.
#ifndef RAYTILES_C_LIBRARY_H
#define RAYTILES_C_LIBRARY_H

#include "raylib.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Tunable parameters for a streamer instance. Mirrors `raytiles::config`.
/// Prefer `RaytilesConfigDefault()` to obtain a defaulted instance, then
/// override individual fields. See `raytiles.h` for the field-by-field
/// semantics; the field names are kept identical.
typedef struct RaytilesConfig {
    /// Lowest LOD zoom that will ever be loaded.
    int    base_zoom;
    /// Highest LOD zoom (tiles directly under the camera).
    int    max_zoom;
    /// World size (meters) of one tile at `base_zoom`.
    float  base_zoom_tile_size;
    /// Radius of the loaded-tile disc, in `base_zoom` tiles.
    int    rendering_radius;
    /// Skirt geometry overlap factor (per side) used to hide LOD seams.
    float  skirt_size;
    /// Heightmap multiplier (drama factor).
    float  height_scale;
    /// Normals XY multiplier (lighting contrast factor).
    float  normals_scale;
    /// Squared XZ distance the camera must travel to trigger a re-stream.
    float  update_distance;
    /// Altitude delta (meters) that triggers a re-stream.
    float  update_height;
    /// Per-frame wall-clock budget (seconds) for promoting tiles to GPU.
    double upload_budget_sec;
    /// Hard cap on tile promotions per frame.
    int    max_uploads_per_frame;
    /// World-space anchor in `base_zoom` tile coordinates (X).
    int    anchor_x_tile;
    /// World-space anchor in `base_zoom` tile coordinates (Z).
    int    anchor_z_tile;
    /// Near clip plane (meters).
    double near_plane;
    /// Far clip plane (meters).
    double far_plane;
    /// Generate trilinear / anisotropic mipmaps for the albedo texture.
    bool   use_mipmap;
    /// Vertical skirt drop (meters) below each tile edge.
    float  skirt_drop;
    /// Distance (meters) at which fog begins.
    float  fog_start;
    /// Distance (meters) at which fog reaches full opacity.
    float  fog_end;
    /// Whether the streamer logs from the main thread via raylib's TraceLog.
    bool   use_logger;
} RaytilesConfig;

/// Pool / provider parameters. Mirrors `raytiles::pool_config`.
/// String fields may be NULL to mean "empty"; otherwise they are copied into
/// the underlying C++ pool on `RaytilesStreamerCreate`.
typedef struct RaytilesPoolConfig {
    /// Number of background download workers.
    int         download_threads;
    /// Skip TLS certificate verification (test/proxy use only).
    bool        allow_insecure_tls;
    /// Whether the pool's worker threads emit log lines.
    bool        use_logger;

    /// On-disk cache path templates, formatted with `{zoom}/{x}/{y}` (or
    /// `{zoom}/{y}/{x}` for the texture path) via `std::vformat`. Parent
    /// directories are created on demand.
    const char *texture_cache_path;
    const char *heightmap_cache_path;
    const char *normals_cache_path;

    /// Provider URL components. The full request URL is
    /// `<host><url_path>` with `{zoom}/{x}/{y}` (or `{y}/{x}`) substituted.
    /// Any provider following the XYZ (Slippy map) convention works.
    const char *texture_host;
    const char *texture_url_path;
    const char *heightmap_host;
    const char *heightmap_url_path;
    const char *normals_host;
    const char *normals_url_path;
} RaytilesPoolConfig;

/// Opaque streamer handle. Allocated with `RaytilesStreamerCreate`,
/// freed with `RaytilesStreamerDestroy`.
typedef struct RaytilesStreamer RaytilesStreamer;

/// Returns a config populated with the same defaults as `raytiles::config{}`.
RaytilesConfig     RaytilesConfigDefault(void);

/// Returns a pool config populated with the same defaults as
/// `raytiles::pool_config{}`. String fields point to static storage owned by
/// the library; do not free them and do not retain the returned struct after
/// the library is unloaded.
RaytilesPoolConfig RaytilesPoolConfigDefault(void);

/// Creates a streamer. Requires a live raylib GL context (`InitWindow` first).
/// Both `conf` and `pool_conf` must be non-NULL. The structs (and the strings
/// referenced by `pool_conf`) are copied; the caller may free them on return.
/// Returns NULL on allocation failure or invalid arguments.
RaytilesStreamer *RaytilesStreamerCreate(const RaytilesConfig     *conf,
                                         const RaytilesPoolConfig *pool_conf);

/// Destroys a streamer and releases all GPU / CPU resources. NULL-safe.
void RaytilesStreamerDestroy(RaytilesStreamer *streamer);

/// Updates the desired tile set based on the camera and promotes any finished
/// downloads to GPU. Cheap to call every frame; internally rate-limited by
/// `RaytilesConfig::upload_budget_sec` and `max_uploads_per_frame`.
void RaytilesStreamerUpdate(RaytilesStreamer *streamer, Camera3D camera);

/// Renders all currently loaded tiles. Call between `BeginMode3D` /
/// `EndMode3D` with the same camera passed to `RaytilesStreamerUpdate`.
void RaytilesStreamerDraw(RaytilesStreamer *streamer, Camera3D camera);

/// Draws a 2D debug HUD with per-tile zoom levels. Call between
/// `BeginDrawing` / `EndDrawing`, after `EndMode3D`.
void RaytilesStreamerDebug(RaytilesStreamer *streamer, Camera3D camera);

/// Draws 3D debug overlays (tile bounds). Call inside the same
/// `BeginMode3D` / `EndMode3D` block as `RaytilesStreamerDraw`.
void RaytilesStreamerDebug3D(RaytilesStreamer *streamer, Camera3D camera);

/// Sets the ambient light color used by the displacement shader. Use this to
/// drive day / night / weather lighting changes.
void RaytilesStreamerSetAmbientLight(RaytilesStreamer *streamer, Color color);

/// Sets the fog color for distance attenuation. Match this to your sky color
/// for a seamless horizon.
void RaytilesStreamerSetFogColor(RaytilesStreamer *streamer, Color color);

/// Sets the distance (meters) at which fog begins blending in.
void RaytilesStreamerSetFogStart(RaytilesStreamer *streamer, float distance);

/// Sets the distance (meters) at which fog reaches full opacity.
void RaytilesStreamerSetFogEnd(RaytilesStreamer *streamer, float distance);

/// Sets the heightmap multiplier (drama factor) used by the vertex shader.
void RaytilesStreamerSetHeightScale(RaytilesStreamer *streamer, float scale);

/// Sets the normals XY multiplier used by the fragment shader. Higher values
/// produce stronger lighting contrast; very large values may produce non
/// physical normals once renormalized.
void RaytilesStreamerSetNormalsScale(RaytilesStreamer *streamer, float scale);

/// Sets the sun direction vector used by the fragment shader's lighting.
/// The shader normalizes the vector internally; magnitude is irrelevant.
void RaytilesStreamerSetSunDirection(RaytilesStreamer *streamer, Vector3 direction);

/// Sets the sun lighting intensity, controlling contrast between lit and
/// shaded slopes.
void RaytilesStreamerSetSunScale(RaytilesStreamer *streamer, float scale);

/// Samples the terrain altitude (world Y) under `position`, reading the
/// heightmap pixel at the equivalent UV. O(1) cost.
///
/// On success, writes the altitude to `*out_height` and returns true.
/// Returns false if no loaded tile covers the queried XZ point or if
/// `streamer` is NULL; `*out_height` is left untouched. `out_height` may be
/// NULL if the caller only wants to test for coverage.
bool RaytilesStreamerGroundHeight(RaytilesStreamer *streamer,
                                  Vector3 position,
                                  float  *out_height);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // RAYTILES_C_LIBRARY_H
