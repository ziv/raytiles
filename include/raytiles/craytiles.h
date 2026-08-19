/// @file craytiles.h
/// C wrapper for the raytiles public API (see raytiles.h).
///
/// Mirrors the C++ API 1:1 in C-compatible form:
///   - `raytiles::config` -> `RaytilesConfig` (nested `RaytilesWorldConfig`,
///     `RaytilesStreamingConfig`, `RaytilesRenderingConfig`,
///     `RaytilesNetworkConfig`), defaults via `RaytilesConfigDefault()`
///   - `raytiles::streamer` -> opaque `RaytilesStreamer*`
///
/// Per-zoom `std::array` fields are exposed as fixed-size C arrays of length
/// `RAYTILES_ZOOM_LEVELS`. Slot `i` applies to zoom `base_zoom + i`; trailing
/// slots beyond `max_zoom - base_zoom` are ignored.
///
/// String fields in `RaytilesNetworkConfig` are `const char*`. NULL is treated
/// as "use the default". Strings only need to remain valid for the duration of
/// the `RaytilesStreamerCreate` call.
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
    /// World-space anchor in tile coordinates at `base_zoom`. The lat/lon
    /// constructor overwrites both.
    int anchor_x_tile;
    int anchor_z_tile;

    /// Lowest LOD zoom that will ever be loaded (in `[9, 15]`).
    int base_zoom;

    /// Highest LOD zoom (tiles directly under the camera).
    int max_zoom;

    /// World size (meters) of one tile at `base_zoom`.
    float tile_size;

    /// Per-zoom skirt overlap factors (baked into generated meshes). Indexed
    /// as `skirt_overlap[zoom - base_zoom]`.
    float skirt_overlap[RAYTILES_ZOOM_LEVELS];

    /// Generate trilinear / anisotropic mipmaps for the albedo texture.
    bool mipmaps;

    /// World-space offset of the anchor point inside its anchor tile.
    Vector3 origin_offset;
} RaytilesWorldConfig;

/// Tile-streaming parameters. Mirrors `raytiles::streaming_config`.
typedef struct RaytilesStreamingConfig {
    /// Radius of the loaded-tile disc, in `base_zoom` tiles.
    int radius;

    /// Per-zoom subdivision distance thresholds (meters). Indexed as
    /// `thresholds[zoom - base_zoom]`.
    float thresholds[RAYTILES_ZOOM_LEVELS];

    /// Distance (meters) the camera must travel to trigger a re-stream.
    float update_distance;

    /// Per-frame wall-clock budget (seconds) for promoting tiles to GPU.
    double upload_budget_sec;

    /// Hard cap on tile promotions per frame.
    int max_uploads_per_frame;

    /// Near clip plane (meters) used for frustum culling.
    double near_plane;

    /// Far clip plane (meters) used for frustum culling.
    double far_plane;
} RaytilesStreamingConfig;

/// Rendering / shader parameters. Mirrors `raytiles::rendering_config`.
typedef struct RaytilesRenderingConfig {
    /// Distance (meters) at which fog begins.
    float fog_start;

    /// Distance (meters) at which fog reaches full opacity.
    float fog_end;

    /// Vertical skirt drop (meters) below each tile edge. 0 disables.
    float skirt_drop;

    /// Fog color. Match this to your sky color.
    Color fog_color;

    /// World ambient color.
    Color ambient_light;

    /// Sun direction vector (the shader normalizes it internally).
    Vector3 sun_direction;

    /// Sun lighting intensity.
    float sun_scale;

    /// Heightmap multiplier (drama factor).
    float height_scale;

    /// Normals multiplier (lighting contrast factor).
    float normals_scale;
} RaytilesRenderingConfig;

/// Tile download / cache parameters. Mirrors `raytiles::network_config`.
/// String fields may be NULL to mean "use the default"; otherwise they are
/// copied on `RaytilesStreamerCreate`.
typedef struct RaytilesNetworkConfig {
    /// Number of background download workers.
    int threads;

    /// Skip TLS certificate verification (test / proxy use only).
    bool allow_insecure_tls;

    /// HTTP connection / read timeouts (seconds).
    int connection_timeout_sec;
    int read_timeout_sec;

    /// Root directory of the on-disk tile cache
    /// (`cache_dir/{texture,heightmap,normals}/zoom/x/y.png`).
    const char *cache_dir;

    /// Provider URL templates (full URL with `:zoom:`/`:x:`/`:y:` tokens).
    const char *texture_url;
    const char *heightmap_url;
    const char *normals_url;
} RaytilesNetworkConfig;

/// The complete streamer configuration. Mirrors `raytiles::config`.
typedef struct RaytilesConfig {
    RaytilesWorldConfig world;
    RaytilesStreamingConfig streaming;
    RaytilesRenderingConfig rendering;
    RaytilesNetworkConfig network;
} RaytilesConfig;

/// Returns a `RaytilesConfig` populated with the same defaults as the C++
/// `raytiles::config{}`. String fields point to static storage owned by the
/// library; do not free them.
RaytilesConfig RaytilesConfigDefault(void);

// ---------------------------------------------------------------------------
//  Streamer
// ---------------------------------------------------------------------------

/// Opaque streamer handle. Allocated with `RaytilesStreamerCreate`,
/// freed with `RaytilesStreamerDestroy`.
typedef struct RaytilesStreamer RaytilesStreamer;

/// Creates a streamer. Requires a live raylib GL context (`InitWindow` first).
/// `config` may be NULL to use all defaults (equivalent to passing
/// `RaytilesConfigDefault()`). The struct (and any strings / arrays it
/// references) is copied; the caller may free it on return. Returns NULL on
/// failure.
RaytilesStreamer *RaytilesStreamerCreate(const RaytilesConfig *config);

/// Creates a streamer anchored at a geographic `latitude` / `longitude`
/// (degrees); the anchor tile, tile size, and origin offset fields of
/// `config->world` are derived from the coordinate. Mirrors the C++
/// `raytiles::streamer(double latitude, double longitude, config)`
/// constructor. `config` may be NULL for defaults. Returns NULL on failure.
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
/// disable. Convention: `absolute = camera.position - worldOffset`.
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

/// Returns true during the initial loading phase. Returns false if `streamer`
/// is NULL.
bool RaytilesStreamerIsLoading(const RaytilesStreamer *streamer);

/// Returns the fraction of the desired tile set that is loaded, in `[0, 1]`.
/// Returns 0 if `streamer` is NULL. Pair with `RaytilesStreamerIsLoading` to
/// drive a splash screen. Mirrors `streamer::loading_progress`.
float RaytilesStreamerLoadingProgress(const RaytilesStreamer *streamer);

/// Returns a sensible initial camera position above the world anchor, raised
/// by `altitude` meters. Returns `(Vector3){0}` if `streamer` is NULL.
/// Mirrors `streamer::initial_position`.
Vector3 RaytilesStreamerInitialPosition(const RaytilesStreamer *streamer, float altitude);

/// Samples the terrain altitude (world Y) under `position`, bilinearly from
/// the resident tile's height grid. O(1) cost.
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
//  Rendering parameter setters
// ---------------------------------------------------------------------------

/// Pushes a whole rendering configuration to the shader in one call.
/// Mirrors `streamer::set_rendering`.
void RaytilesStreamerSetRendering(RaytilesStreamer *streamer, RaytilesRenderingConfig rendering);

/// Sets the fog color for distance attenuation. Match this to your sky color
/// for a seamless horizon.
void RaytilesStreamerSetFogColor(RaytilesStreamer *streamer, Color color);

/// Sets the distance (meters) at which fog begins blending in.
void RaytilesStreamerSetFogStart(RaytilesStreamer *streamer, float distance);

/// Sets the distance (meters) at which fog reaches full opacity.
void RaytilesStreamerSetFogEnd(RaytilesStreamer *streamer, float distance);

/// Sets the ambient light color sent to the displacement shader. Use this to
/// drive day / night / weather lighting changes.
void RaytilesStreamerSetAmbientLight(RaytilesStreamer *streamer, Color color);

/// Sets the sun direction vector used by the fragment shader's lighting.
/// The shader normalizes the vector internally; magnitude is irrelevant.
void RaytilesStreamerSetSunDirection(RaytilesStreamer *streamer, Vector3 direction);

/// Sets the sun lighting intensity, controlling contrast between lit and
/// shaded slopes.
void RaytilesStreamerSetSunScale(RaytilesStreamer *streamer, float scale);

/// Sets the heightmap multiplier (drama factor) used by the vertex shader.
void RaytilesStreamerSetHeightScale(RaytilesStreamer *streamer, float scale);

/// Sets the normals multiplier used by the fragment shader. Higher values
/// produce stronger lighting contrast.
void RaytilesStreamerSetNormalsScale(RaytilesStreamer *streamer, float scale);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // RAYTILES_C_LIBRARY_H
