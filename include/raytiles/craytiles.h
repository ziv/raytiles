/// @file craytiles.h
/// C wrapper for the raytiles public API (see raytiles.h).
///
/// Mirrors the C++ API 1:1 in C-compatible form:
///   - `raytiles::world_config`     -> `RaytilesWorldConfig`
///                                     (default: `RaytilesWorldConfigDefault()`)
///   - `raytiles::streaming_config` -> `RaytilesStreamingConfig`
///                                     (default: `RaytilesStreamingConfigDefault()`)
///   - `raytiles::rendering_config` -> `RaytilesRenderingConfig`
///                                     (default: `RaytilesRenderingConfigDefault()`)
///   - `raytiles::pool_config`      -> `RaytilesPoolConfig`
///                                     (default: `RaytilesPoolConfigDefault()`)
///   - `raytiles::streamer`         -> opaque `RaytilesStreamer*`
///   - `raytiles::renderer`         -> opaque `RaytilesRenderer*`, obtained via
///                                     `RaytilesStreamerGetRenderer`
///
/// Per-zoom `std::unordered_map` fields are exposed as parallel arrays
/// (`*_zooms`, `*_values`, `*_count`). The arrays only need to remain valid
/// for the duration of the `RaytilesStreamerCreate` call; the C++ side
/// copies them into the underlying map. NULL arrays mean "use defaults".
///
/// String fields in `RaytilesPoolConfig` are `const char*`. NULL is treated as
/// an empty string. Strings only need to remain valid for the duration of the
/// `RaytilesStreamerCreate` call.
///
/// `Camera3D`, `Vector3`, `Vector4`, and `Color` are passed by value to keep
/// the ABI C-compatible (no C++ references in the public surface).
///
/// All functions that touch GPU state require a live raylib GL context
/// (`InitWindow` first), matching the C++ contract.
#ifndef RAYTILES_C_LIBRARY_H
#define RAYTILES_C_LIBRARY_H

#include "raylib.h"

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

    /// Per-zoom skirt overlap factors (baked into generated meshes). Parallel
    /// arrays mirroring `raytiles::world_config::skirt_overlap`. Must cover
    /// every zoom in `[base_zoom, max_zoom]`. NULL means "use defaults".
    const int *skirt_overlap_zooms;
    const float *skirt_overlap_values;
    int skirt_overlap_count;

    /// Generate trilinear / anisotropic mipmaps for the albedo texture.
    bool use_mipmap;

    /// Whether the streamer logs from the main thread via raylib's TraceLog.
    bool use_logger;
} RaytilesWorldConfig;

/// Tile-streaming parameters. Mirrors `raytiles::streaming_config`.
typedef struct RaytilesStreamingConfig {
    /// Radius of the loaded-tile disc, in `base_zoom` tiles.
    int rendering_radius;

    /// Per-zoom subdivision distance thresholds. Parallel arrays mirroring
    /// `raytiles::streaming_config::thresholds`. Must cover every zoom in
    /// `[base_zoom, max_zoom]`. NULL means "use defaults".
    const int *threshold_zooms;
    const float *threshold_values;
    int thresholds_count;

    /// Squared XZ distance the camera must travel to trigger a re-stream.
    /// Type matches the C++ `MetersSq` (double).
    double update_distance_sq;

    /// Altitude delta (meters) that triggers a re-stream.
    float update_height;

    /// Per-frame wall-clock budget (seconds) for promoting tiles to GPU.
    double upload_budget_sec;

    /// Hard cap on tile promotions per frame.
    int max_uploads_per_frame;
} RaytilesStreamingConfig;

/// Rendering / shader-uniform parameters. Mirrors `raytiles::rendering_config`.
typedef struct RaytilesRenderingConfig {
    /// Near clip plane (meters). Matches the C++ `MetersD` (double).
    double near_plane;

    /// Far clip plane (meters).
    double far_plane;

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

/// Pool / provider parameters. Mirrors `raytiles::pool_config`.
/// String fields may be NULL to mean "empty"; otherwise they are copied into
/// the underlying C++ pool on `RaytilesStreamerCreate`.
typedef struct RaytilesPoolConfig {
    /// Number of background download workers.
    int download_threads;

    /// Skip TLS certificate verification (test / proxy use only).
    bool allow_insecure_tls;

    /// Whether the pool's worker threads emit log lines.
    bool use_logger;

    /// On-disk cache path templates, formatted with `{zoom}/{x}/{y}` (or
    /// `{zoom}/{y}/{x}` for the texture path) via `std::vformat`.
    const char *texture_cache_path;
    const char *heightmap_cache_path;
    const char *normals_cache_path;

    /// Provider URL templates (full URL, including scheme + host + path).
    /// If `*_host` and `*_url_path` are both NULL/empty, the pool splits the
    /// full URL into host and path at construction.
    const char *texture_url;
    const char *heightmap_url;
    const char *normals_url;

    /// Pre-split provider URL components. If non-empty they take precedence
    /// over the full `*_url` strings above. The full request URL is
    /// `<host><url_path>` with `{zoom}/{x}/{y}` (or `{y}/{x}`) substituted.
    const char *texture_host;
    const char *texture_url_path;
    const char *heightmap_host;
    const char *heightmap_url_path;
    const char *normals_host;
    const char *normals_url_path;
} RaytilesPoolConfig;

// ---------------------------------------------------------------------------
//  Default-initializers
// ---------------------------------------------------------------------------

/// Returns a `RaytilesWorldConfig` populated with the same defaults as the
/// C++ `raytiles::world_config{}`. The per-zoom array pointers point at
/// library-owned static storage valid for the lifetime of the process.
RaytilesWorldConfig RaytilesWorldConfigDefault(void);

/// Returns a `RaytilesStreamingConfig` populated with the same defaults as the
/// C++ `raytiles::streaming_config{}`. The per-zoom array pointers point at
/// library-owned static storage valid for the lifetime of the process.
RaytilesStreamingConfig RaytilesStreamingConfigDefault(void);

/// Returns a `RaytilesRenderingConfig` populated with the same defaults as
/// the C++ `raytiles::rendering_config{}`.
RaytilesRenderingConfig RaytilesRenderingConfigDefault(void);

/// Returns a `RaytilesPoolConfig` populated with the same defaults as the
/// C++ `raytiles::pool_config{}`. String fields point to static storage owned
/// by the library; do not free them.
RaytilesPoolConfig RaytilesPoolConfigDefault(void);

// ---------------------------------------------------------------------------
//  Streamer
// ---------------------------------------------------------------------------

/// Opaque streamer handle. Allocated with `RaytilesStreamerCreate`,
/// freed with `RaytilesStreamerDestroy`.
typedef struct RaytilesStreamer RaytilesStreamer;

/// Opaque renderer handle. Non-owning view onto the streamer's renderer;
/// obtained via `RaytilesStreamerGetRenderer`. Do not free.
typedef struct RaytilesRenderer RaytilesRenderer;

/// Creates a streamer. Requires a live raylib GL context (`InitWindow` first).
/// Any of the config pointers may be NULL to use the corresponding C++ default
/// (equivalent to passing `RaytilesXxxConfigDefault()`). The structs (and any
/// strings / arrays they reference) are copied; the caller may free them on
/// return. Returns NULL on allocation failure.
RaytilesStreamer *RaytilesStreamerCreate(const RaytilesWorldConfig *world,
                                         const RaytilesStreamingConfig *streaming,
                                         const RaytilesRenderingConfig *rendering,
                                         const RaytilesPoolConfig *pool);

/// Destroys a streamer and releases all GPU / CPU resources. NULL-safe.
void RaytilesStreamerDestroy(RaytilesStreamer *streamer);

/// Updates the desired tile set based on `camera` and promotes any finished
/// downloads into GPU resources. Cheap to call every frame; internally
/// rate-limited by `RaytilesStreamingConfig::upload_budget_sec` and
/// `max_uploads_per_frame`.
void RaytilesStreamerUpdate(RaytilesStreamer *streamer, Camera3D camera);

/// Renders all currently loaded tiles. Call between `BeginMode3D` /
/// `EndMode3D` with the same camera passed to `RaytilesStreamerUpdate`.
void RaytilesStreamerDraw(RaytilesStreamer *streamer, Camera3D camera);

/// Draws a 2D HUD with streamer statistics (loaded / loading counts, etc.)
/// and zoom labels above the tiles. Call between `BeginDrawing` /
/// `EndDrawing`, after `EndMode3D`.
void RaytilesStreamerDebug(RaytilesStreamer *streamer, Camera3D camera);

/// Draws 3D debug overlays (tile bounds). Call inside the same
/// `BeginMode3D` / `EndMode3D` block as `RaytilesStreamerDraw`.
void RaytilesStreamerDebug3D(RaytilesStreamer *streamer);

/// Returns a non-owning handle to the streamer's renderer for direct access
/// to shader-parameter setters. Returns NULL if `streamer` is NULL. Do not
/// free the returned pointer; its lifetime is tied to `streamer`.
RaytilesRenderer *RaytilesStreamerGetRenderer(RaytilesStreamer *streamer);

/// Returns true during the initial loading phase (i.e. while at least one
/// tile required to fill the rendering radius is still being fetched).
/// Returns false if `streamer` is NULL.
bool RaytilesStreamerIsLoading(const RaytilesStreamer *streamer);

/// Returns the initial-load progress in `[0, 1]`. Returns 0 if `streamer` is
/// NULL. Pair with `RaytilesStreamerIsLoading` to drive a splash screen.
float RaytilesStreamerGetLoading(const RaytilesStreamer *streamer);

/// Samples the terrain altitude (world Y) under `position`, reading the
/// heightmap pixel at the equivalent UV. O(1) cost.
///
/// On success, writes the altitude to `*out_height` and returns true.
/// Returns false if no loaded tile covers the queried XZ point or if
/// `streamer` is NULL; `*out_height` is left untouched. `out_height` may be
/// NULL if the caller only wants to test for coverage.
bool RaytilesStreamerGroundHeight(const RaytilesStreamer *streamer,
                                  Vector3 position,
                                  float *out_height);

// ---------------------------------------------------------------------------
//  Renderer
// ---------------------------------------------------------------------------

/// Sets the ambient light color sent to the displacement shader. Use this to
/// drive day / night / weather lighting changes.
/// Three variants mirror the C++ overloads: `Color` (8-bit per channel),
/// `Vector4` (normalized 0..1 floats), and explicit float RGBA components.
void RaytilesRendererSetAmbientLight(RaytilesRenderer *renderer, Color color);
void RaytilesRendererSetAmbientLightV4(RaytilesRenderer *renderer, Vector4 color);
void RaytilesRendererSetAmbientLightRGBA(RaytilesRenderer *renderer,
                                         float r, float g, float b, float a);

/// Sets the fog color for distance attenuation. Match this to your sky color
/// for a seamless horizon.
/// Three variants mirror the C++ overloads: `Color` (8-bit per channel),
/// `Vector4` (normalized 0..1 floats), and explicit float RGBA components.
void RaytilesRendererSetFogColor(RaytilesRenderer *renderer, Color color);
void RaytilesRendererSetFogColorV4(RaytilesRenderer *renderer, Vector4 color);
void RaytilesRendererSetFogColorRGBA(RaytilesRenderer *renderer,
                                     float r, float g, float b, float a);

/// Sets the distance (meters) at which fog begins blending in.
void RaytilesRendererSetFogStart(RaytilesRenderer *renderer, float distance);

/// Sets the distance (meters) at which fog reaches full opacity.
void RaytilesRendererSetFogEnd(RaytilesRenderer *renderer, float distance);

/// Sets the heightmap multiplier (drama factor) used by the vertex shader.
void RaytilesRendererSetHeightScale(RaytilesRenderer *renderer, float scale);

/// Sets the normals multiplier used by the fragment shader. Higher values
/// produce stronger lighting contrast.
void RaytilesRendererSetNormalsScale(RaytilesRenderer *renderer, float scale);

/// Sets the sun direction vector used by the fragment shader's lighting.
/// The shader normalizes the vector internally; magnitude is irrelevant.
void RaytilesRendererSetSunDirection(RaytilesRenderer *renderer, Vector3 direction);

/// Sets the sun lighting intensity, controlling contrast between lit and
/// shaded slopes.
void RaytilesRendererSetSunScale(RaytilesRenderer *renderer, float scale);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // RAYTILES_C_LIBRARY_H
