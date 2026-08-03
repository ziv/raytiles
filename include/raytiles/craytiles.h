/// @file craytiles.h
/// C wrapper for the raytiles public API (see raytiles.h).
///
/// Mirrors the C++ API 1:1 in C-compatible form:
///   - `raytiles::config`           -> `RaytilesConfig`
///                                     (default: `RaytilesConfigDefault()`)
///     with the same nested groups: `world`, `streaming`, `rendering`,
///     `network`.
///   - `raytiles::streamer`         -> opaque `RaytilesStreamer*`
///
/// Shader-parameter setters are exposed directly on the streamer handle as
/// `RaytilesStreamerSet*` (raylib `Color` / `Vector3` based, matching the
/// C++ setters; float-precision colors go through
/// `RaytilesRenderingConfig` at creation).
///
/// Per-zoom `std::array` fields are exposed as fixed-size C arrays of length
/// `RAYTILES_ZOOM_LEVELS`. Slot `i` applies to zoom `base_zoom + i`; trailing
/// slots beyond `max_zoom - base_zoom` are ignored.
///
/// String fields in `RaytilesNetworkConfig` are `const char*`. NULL is treated
/// as the built-in default. Strings only need to remain valid for the duration
/// of the `RaytilesStreamerCreate*` call.
///
/// `Camera3D`, `Vector3`, and `Color` are passed by value to keep the ABI
/// C-compatible (no C++ references in the public surface).
///
/// All functions that touch GPU state require a live raylib GL context
/// (`InitWindow` first), matching the C++ contract.
#ifndef RAYTILES_C_LIBRARY_H
#define RAYTILES_C_LIBRARY_H

#include "raylib.h"

/// Number of zoom levels in `[9, 15]`. Sizes the per-zoom arrays
/// `RaytilesWorldConfig::skirt_overlap` and `RaytilesStreamingConfig::thresholds`.
/// Must match `raytiles::zoom_levels` on the C++ side.
#define RAYTILES_ZOOM_LEVELS 7

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
//  Configuration structs
// ---------------------------------------------------------------------------

/// World topology / geometry parameters. Mirrors `raytiles::world_config`.
/// See `raytiles.h` for field-by-field semantics; names are kept identical.
typedef struct RaytilesWorldConfig {
    /// World-space anchor in tile coordinates at `base_zoom`.
    int anchor_x_tile;
    int anchor_z_tile;

    /// Lowest LOD zoom that will ever be loaded.
    int base_zoom;

    /// Highest LOD zoom (tiles directly under the camera).
    int max_zoom;

    /// World size (meters) of one tile at `base_zoom`.
    float base_zoom_tile_size;

    /// Per-zoom skirt overlap factors (baked into generated meshes). Indexed
    /// as `skirt_overlap[zoom - base_zoom]`. Mirrors
    /// `raytiles::world_config::skirt_overlap`.
    float skirt_overlap[RAYTILES_ZOOM_LEVELS];

    /// Generate trilinear / anisotropic mipmaps for the albedo texture.
    bool use_mipmap;

    /// Initial-position hint (world-space point of the anchor). Mirrors
    /// `raytiles::world_config::offset`; filled in automatically by
    /// `RaytilesStreamerCreateLatLon`.
    Vector3 offset;
} RaytilesWorldConfig;

/// Tile-streaming parameters. Mirrors `raytiles::streaming_config`.
typedef struct RaytilesStreamingConfig {
    /// Radius of the loaded-tile disc, in `base_zoom` tiles.
    int rendering_radius;

    /// Per-zoom subdivision distance thresholds. Indexed as
    /// `thresholds[zoom - base_zoom]`. Mirrors
    /// `raytiles::streaming_config::thresholds`.
    float thresholds[RAYTILES_ZOOM_LEVELS];

    /// Distance (meters) the camera must travel to trigger a re-stream.
    /// Mirrors `raytiles::streaming_config::update_distance`.
    float update_distance;

    /// Per-frame wall-clock budget (seconds) for promoting tiles to GPU.
    double upload_budget_sec;

    /// Hard cap on tile promotions per frame.
    int max_uploads_per_frame;

    /// Near clip plane (meters) used for frustum extraction.
    double near_plane;

    /// Far clip plane (meters) used for frustum extraction.
    double far_plane;
} RaytilesStreamingConfig;

/// Rendering / shader-uniform parameters. Mirrors `raytiles::rendering_config`.
typedef struct RaytilesRenderingConfig {
    /// Distance (meters) at which fog begins.
    float fog_start;

    /// Distance (meters) at which fog reaches full opacity.
    float fog_end;

    /// Vertical skirt drop (meters) below each tile edge. 0 disables.
    float skirt_drop;

    /// Fog color (RGBA, 0..1).
    float fog_color[4];

    /// World ambient color (RGBA, 0..1).
    float ambient_light[4];

    /// Sun direction vector (shader normalizes it internally).
    float sun_direction[3];

    /// Sun lighting intensity.
    float sun_scale;

    /// Heightmap multiplier (drama factor).
    float height_scale;

    /// Normals multiplier (lighting contrast factor).
    float normals_scale;
} RaytilesRenderingConfig;

/// Download / provider parameters. Mirrors `raytiles::network_config`.
/// String fields may be NULL to mean "use the built-in default"; otherwise
/// they are copied on `RaytilesStreamerCreate*`.
typedef struct RaytilesNetworkConfig {
    /// Number of background download workers.
    int download_threads;

    /// Skip TLS certificate verification (test / proxy use only).
    bool allow_insecure_tls;

    /// On-disk cache path templates, formatted with `{zoom}/{x}/{z}` slots
    /// (`{}` placeholders) via `std::vformat`.
    const char *texture_cache_path;
    const char *heightmap_cache_path;
    const char *normals_cache_path;

    /// Provider URL templates (full URL with `:zoom:`/`:x:`/`:y:` tokens).
    const char *texture_url;
    const char *heightmap_url;
    const char *normals_url;
} RaytilesNetworkConfig;

/// Complete streamer configuration. Mirrors `raytiles::config`: four nested
/// groups, all defaulted by `RaytilesConfigDefault()`.
typedef struct RaytilesConfig {
    RaytilesWorldConfig world;
    RaytilesStreamingConfig streaming;
    RaytilesRenderingConfig rendering;
    RaytilesNetworkConfig network;
} RaytilesConfig;

// ---------------------------------------------------------------------------
//  Default-initializer
// ---------------------------------------------------------------------------

/// Returns a `RaytilesConfig` populated with the same defaults as the C++
/// `raytiles::config{}` — a fully working configuration (free Esri imagery +
/// Mapzen terrain). String fields point to static storage owned by the
/// library; do not free them.
RaytilesConfig RaytilesConfigDefault(void);

// ---------------------------------------------------------------------------
//  Streamer
// ---------------------------------------------------------------------------

/// Opaque streamer handle. Allocated with `RaytilesStreamerCreate`,
/// freed with `RaytilesStreamerDestroy`.
typedef struct RaytilesStreamer RaytilesStreamer;

/// Creates a streamer. Requires a live raylib GL context (`InitWindow` first).
/// `config` may be NULL to use `RaytilesConfigDefault()`. The struct (and any
/// strings / arrays it references) is copied; the caller may free it on
/// return. Returns NULL on failure (allocation or invalid zoom bounds).
RaytilesStreamer *RaytilesStreamerCreate(const RaytilesConfig *config);

/// Creates a streamer anchored at a geographic `latitude` / `longitude`
/// (degrees): the anchor tiles, tile size, and initial-position offset of
/// `config->world` are computed from the coordinate; everything else in
/// `config` is used as-is. Mirrors the C++
/// `raytiles::streamer(latitude, longitude, config)` constructor.
/// `config` may be NULL to use `RaytilesConfigDefault()`. Requires a live
/// raylib GL context (`InitWindow` first). Returns NULL on failure.
RaytilesStreamer *RaytilesStreamerCreateLatLon(double latitude,
                                               double longitude,
                                               const RaytilesConfig *config);

/// Destroys a streamer and releases all GPU / CPU resources. NULL-safe.
void RaytilesStreamerDestroy(RaytilesStreamer *streamer);

/// Updates the desired tile set based on `camera` and promotes any finished
/// downloads into GPU resources. Cheap to call every frame; internally
/// rate-limited by `RaytilesStreamingConfig::upload_budget_sec` and
/// `max_uploads_per_frame`.
///
/// Caches `camera` and `worldOffset` for use by the matching
/// `RaytilesStreamerDraw` and `RaytilesStreamerGroundHeight` calls in the
/// same frame. Call once per frame, *after* applying any large-world rebase
/// to your scene, before the matching draw / ground-height queries.
///
/// `worldOffset` implements large-world shifting: pass `(Vector3){0}` to
/// disable. Convention: `absolute = camera.position - worldOffset`,
/// equivalently `camera.position = absolute + worldOffset`.
void RaytilesStreamerUpdate(RaytilesStreamer *streamer, Camera3D camera, Vector3 worldOffset);

/// Renders all currently loaded tiles. Call between `BeginMode3D` /
/// `EndMode3D` after `RaytilesStreamerUpdate` in the same frame. Reuses the
/// camera and `worldOffset` cached by `RaytilesStreamerUpdate`.
void RaytilesStreamerDraw(RaytilesStreamer *streamer);

/// Renders debug 3D geometry (tile bounds / wireframes) for the current tile
/// set. Call between `BeginMode3D` / `EndMode3D` after
/// `RaytilesStreamerUpdate`. NULL-safe. Mirrors `streamer::draw_debug_3d`.
void RaytilesStreamerDrawDebug3D(RaytilesStreamer *streamer);

/// Renders debug 2D text labels for the current tile set. Call after
/// `EndMode3D` (in screen space). NULL-safe. Mirrors
/// `streamer::draw_debug_labels`.
void RaytilesStreamerDrawDebugLabels(RaytilesStreamer *streamer);

/// Returns true during the initial loading phase (i.e. while at least one
/// tile required to fill the rendering radius is still being fetched).
/// Returns false if `streamer` is NULL.
bool RaytilesStreamerIsLoading(const RaytilesStreamer *streamer);

/// Returns the initial-load progress in `[0, 1]`, monotonically
/// non-decreasing; returns 1 once loading completed and 0 if `streamer` is
/// NULL. Pair with `RaytilesStreamerIsLoading` to drive a splash screen.
/// Mirrors `streamer::loading_progress`.
float RaytilesStreamerLoadingProgress(const RaytilesStreamer *streamer);

/// Returns a sensible initial camera position above the world anchor, raised
/// by `y` meters on the vertical axis. Useful for placing the camera before
/// any tiles have loaded. Returns `(Vector3){0}` if `streamer` is NULL.
/// Mirrors `streamer::get_initial_position`.
Vector3 RaytilesStreamerGetInitialPosition(const RaytilesStreamer *streamer, float y);

/// Samples the terrain altitude (world Y) under `position`, reading the
/// heightmap pixel at the equivalent UV. O(1) cost.
///
/// `position` is in user space; the `worldOffset` cached by the most
/// recent `RaytilesStreamerUpdate` is applied internally.
///
/// On success, writes the altitude to `*out_height` and returns true.
/// Returns false if no loaded tile covers the queried XZ point or if
/// `streamer` is NULL; `*out_height` is left untouched. `out_height` may be
/// NULL if the caller only wants to test for coverage.
bool RaytilesStreamerGroundHeight(const RaytilesStreamer *streamer,
                                  Vector3 position,
                                  float *out_height);

// ---------------------------------------------------------------------------
//  Shader parameter setters
// ---------------------------------------------------------------------------

/// Sets the ambient light color sent to the displacement shader. Use this to
/// drive day / night / weather lighting changes.
void RaytilesStreamerSetAmbientLight(RaytilesStreamer *streamer, Color color);

/// Sets the fog color for distance attenuation. Match this to your sky color
/// for a seamless horizon.
void RaytilesStreamerSetFogColor(RaytilesStreamer *streamer, Color color);

/// Sets fog color and both fade distances in one call: colors start blending
/// at `start` meters from the camera and are fully replaced by `color` at
/// `end` meters. Mirrors `streamer::set_fog`.
void RaytilesStreamerSetFog(RaytilesStreamer *streamer, Color color, float start, float end);

/// Sets the sun: `direction` is normalized by the shader (magnitude
/// irrelevant); `intensity` controls lit/shaded contrast (default 1.0).
/// Mirrors `streamer::set_sun`.
void RaytilesStreamerSetSun(RaytilesStreamer *streamer, Vector3 direction, float intensity);

/// Sets the heightmap multiplier (drama factor) used by the vertex shader.
void RaytilesStreamerSetHeightScale(RaytilesStreamer *streamer, float scale);

/// Sets the normals multiplier used by the fragment shader. Higher values
/// produce stronger lighting contrast.
void RaytilesStreamerSetNormalsScale(RaytilesStreamer *streamer, float scale);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // RAYTILES_C_LIBRARY_H
