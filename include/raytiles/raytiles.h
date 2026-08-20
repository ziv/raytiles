/// @file raytiles.h
/// Public API for the raytiles library: stream a 3D world built from satellite
/// imagery and heightmap tiles around a moving camera and render it via raylib.
#ifndef RAYTILES_LIBRARY_H
#define RAYTILES_LIBRARY_H
#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include "raylib.h"

#ifndef RAYTILES_TEXTURE_URL
// the order zoom/y/x is not a mistake, that is the way Esri encoded their URLs
#define RAYTILES_TEXTURE_URL "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/:zoom:/:y:/:x:"
#endif

#ifndef RAYTILES_HEIGHTMAP_URL
#define RAYTILES_HEIGHTMAP_URL "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/:zoom:/:x:/:y:.png"
#endif

#ifndef RAYTILES_NORMALS_URL
#define RAYTILES_NORMALS_URL "https://s3.amazonaws.com/elevation-tiles-prod/normal/:zoom:/:x:/:y:.png"
#endif

namespace raytiles {
using Zoom = int;
using Meters = float;
using MetersSq = float;
using MetersD = double;
using MetersDSq = double;

/// Lowest zoom level supported by the library. `world_config::base_zoom`
/// must be `>= min_supported_zoom`; constructing a streamer with a lower
/// `base_zoom` throws `std::runtime_error`.
constexpr Zoom min_supported_zoom = 9;

/// Highest zoom level supported by the library. `world_config::max_zoom`
/// must be `<= max_supported_zoom`; constructing a streamer with a higher
/// `max_zoom` throws `std::runtime_error`. Above
/// `network_config::native_terrain_zoom`, heightmaps are synthesized from
/// their native-zoom ancestors and normals fall back to flat defaults —
/// only imagery is fetched natively that deep.
constexpr Zoom max_supported_zoom = 22;

/// Number of zoom levels in `[min_supported_zoom, max_supported_zoom]`.
/// Sizes the per-zoom arrays `world_config::skirt_overlap` and
/// `streaming_config::thresholds`. Slot `i` corresponds to zoom
/// `base_zoom + i`; slots beyond `max_zoom - base_zoom` are unused.
constexpr std::size_t zoom_levels = max_supported_zoom - min_supported_zoom + 1;

/// World topology / geometry parameters. Everything in this struct is
/// effectively immutable once a `streamer` exists: changing any field
/// requires rebuilding meshes, re-uploading textures, or re-anchoring the
/// world. Set once at construction.
struct world_config {
  /// World-space anchor in tile coordinates at `base_zoom`. The streamer
  /// translates tile XY to world XZ relative to this anchor so the world
  /// origin is wherever you want it (e.g. your runway). The lat/lon
  /// constructor overwrites both.
  int anchor_x_tile = 306;
  int anchor_z_tile = 207;

  /// Lowest level-of-detail zoom that will ever be loaded. Tiles outside
  /// the camera's near radius are kept at this zoom to bound the working
  /// set. Changing this also requires updating
  /// `streaming_config::thresholds` and `tile_size`.
  /// Must be `>= min_supported_zoom`.
  Zoom base_zoom = min_supported_zoom;

  /// Highest level-of-detail zoom available. Tiles directly under the
  /// camera are subdivided up to this zoom. Changing this also requires
  /// updating `streaming_config::thresholds`.
  /// Must be `<= max_supported_zoom` and `>= base_zoom`.
  /// Defaults to the native terrain ceiling (15); raising it beyond
  /// `network_config::native_terrain_zoom` (up to `max_supported_zoom`)
  /// opts into synthesized heightmaps and default normals — see those docs.
  Zoom max_zoom = 15;

  /// World size (in meters) of one tile at `base_zoom`. Tiles at higher
  /// zooms are scaled by `1 / (1 << (zoom - base_zoom))`. The lat/lon
  /// constructor derives it from the latitude.
  Meters tile_size = 66400.0f;

  /// Per-zoom skirt overlap factors, allowing you to tweak the amount of
  /// overlap (and thus fill rate) at different zoom levels. Baked into
  /// generated meshes. Indexed as `skirt_overlap[zoom - base_zoom]`;
  /// slots beyond `max_zoom - base_zoom` are ignored.
  std::array<float, zoom_levels> skirt_overlap = {1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f};

  /// Generate trilinear / anisotropic mipmaps for the albedo texture on
  /// upload. Strongly recommended; avoids shimmering at distance.
  bool mipmaps = true;

  /// World-space offset of the anchor point inside its anchor tile.
  /// The lat/lon constructor fills it so `initial_position()` starts
  /// exactly over the requested coordinate.
  Vector3 origin_offset = {0.0f, 0.0f, 0.0f};
};

/// Tile-streaming parameters. Governs *which* tiles are kept resident and
/// how aggressively the working set is updated.
struct streaming_config {
  /// Radius, in `world_config::base_zoom` tiles, of the disc of tiles
  /// loaded around the camera. Larger values = more tiles in flight =
  /// more memory / bandwidth.
  int radius = 6;

  /// Per-zoom subdivision distance thresholds (meters), covering
  /// `base_zoom` through `max_zoom`. Tuned for performance and to keep
  /// the resident tile count under 600. Indexed as
  /// `thresholds[zoom - base_zoom]`; slots beyond `max_zoom - base_zoom`
  /// are ignored.
  std::array<Meters, zoom_levels> thresholds = {100000.0f, 80000.0f, 40000.0f, 20000.0f, 10000.0f, 5000.0f, 2500.0f,
                                                1250.0f,   625.0f,   312.0f,   156.0f,   78.0f,    39.0f,   20.0f};

  /// Distance (in meters) the camera must travel to trigger a
  /// desired-set recomputation. Keep this large enough that small
  /// movements don't churn the working set.
  Meters update_distance = 100.0f;

  /// Wall-clock budget (in seconds) per frame for promoting downloaded
  /// tiles into GPU resources. Caps the cost of a single bursty frame.
  double upload_budget_sec = 0.002;

  /// Hard cap on tile promotions per frame, on top of
  /// `upload_budget_sec`. Whichever limit is hit first stops the loop.
  int max_uploads_per_frame = 8;

  /// Near clip plane (meters) used for frustum culling. Match this to
  /// your camera setup (`rlSetClipPlanes`).
  double near_plane = 1;

  /// Far clip plane (meters) used for frustum culling. Match this to
  /// your camera setup (`rlSetClipPlanes`).
  double far_plane = 400000;
};

/// Rendering / shader parameters. Every field is runtime-mutable: push a
/// whole struct with `streamer::set_rendering`, or individual values with
/// the matching `streamer::set_*` setters.
struct rendering_config {
  /// Distance (in meters) at which atmospheric fog starts to fade tiles
  /// to `fog_color`.
  Meters fog_start = 100000.0f;

  /// Distance (in meters) at which fog reaches full cover.
  Meters fog_end = 150000.0f;

  /// Vertical drop (in meters) of the skirt geometry below each tile's
  /// edge. Larger values hide cracks more reliably but cost more fill
  /// rate. 0 disables the feature.
  Meters skirt_drop = 0.0f;

  /// Fog color. Match this to your sky color for a seamless horizon.
  Color fog_color = BLUE;

  /// World ambient color. Drives day / night / weather lighting changes.
  Color ambient_light = WHITE;

  /// Sun direction vector. The shader normalizes it internally;
  /// magnitude is irrelevant.
  Vector3 sun_direction = {0.1f, 1.0f, 0.1f};

  /// Sun lighting intensity, controlling contrast between lit and shaded
  /// areas.
  float sun_scale = 1.0f;

  /// Scales the heightmap to exaggerate or flatten the terrain relief
  /// (drama factor).
  float height_scale = 1.0f;

  /// Scales the normals to increase or reduce lighting contrast. Higher
  /// values look bumpier but can cause artifacts if the normals become
  /// too steep.
  float normals_scale = 1.0f;
};

/// Tile download / cache parameters.
struct network_config {
  /// Number of background download workers. Downloads are I/O-bound so
  /// it's safe to use more threads than CPU cores.
  int threads = 4;

  /// Skip TLS certificate verification for tile downloads. Only useful
  /// for local proxies; never enable against a real server.
  bool allow_insecure_tls = false;

  /// HTTP connection / read timeouts (seconds).
  int connection_timeout_sec = 5;
  int read_timeout_sec = 3;

  /// Highest zoom the terrain providers serve natively (Mapzen Terrarium and
  /// its normals stop at 15). Above it, heightmaps are synthesized by
  /// upsampling the native-zoom ancestor (cached like fetched tiles) and
  /// normals fall back to flat defaults — no HTTP is attempted for either.
  /// Must be in `[min_supported_zoom, max_supported_zoom]`.
  Zoom native_terrain_zoom = 15;

  /// Root directory of the on-disk tile cache. Layout:
  /// `cache_dir/{texture,heightmap,normals}/zoom/x/y.png` (the same
  /// layout `scripts/tiles-cache.mjs` pre-warms).
  std::string cache_dir = ".cache";

  /// Provider URL templates with `:zoom:` / `:x:` / `:y:` tokens
  /// substituted at request time. Any provider following the XYZ
  /// (slippy-map) convention works, as long as the heightmap provider
  /// returns Terrarium-encoded heightmaps.
  std::string texture_url = RAYTILES_TEXTURE_URL;
  std::string heightmap_url = RAYTILES_HEIGHTMAP_URL;
  std::string normals_url = RAYTILES_NORMALS_URL;
};

/// The complete streamer configuration. Every field of every sub-struct is
/// defaulted, so `raytiles::config{}` (or designated initializers for the
/// few fields you care about) is all a quick start needs.
struct config {
  world_config world;
  streaming_config streaming;
  rendering_config rendering;
  network_config network;
};

/// Per-frame driver that maintains the working set of tiles around a camera
/// and renders them. One streamer manages one world; create more if you
/// need independent worlds.
///
/// Typical use:
/// @code
///   raytiles::streamer s(latitude, longitude);
///   while (!WindowShouldClose()) {
///     // 1. apply your large-world rebase (if any) so camera and
///     //    world_offset agree on the *current* frame.
///     // 2. hand them to the streamer once via update().
///     s.update(camera, world_offset);
///     BeginDrawing();
///     BeginMode3D(camera);
///     // draw / ground_height reuse the camera + offset cached by update().
///     s.draw();
///     EndMode3D();
///     EndDrawing();
///   }
/// @endcode
///
/// **Frame contract.** `update()` is the single point of truth for the
/// camera and `world_offset` of the current frame. `draw()` and
/// `ground_height()` reuse those values — no need (and no way) to pass
/// them again. Always call `update()` once per frame, *after* applying
/// any large-world rebase, *before* `draw()` and any `ground_height()`
/// queries.
///
/// All raylib resources are owned via RAII; destruction is safe and
/// complete. Movable but not copyable.
class streamer {
 public:
  /// @note A raylib window must already be initialized (`InitWindow`)
  ///       before constructing a streamer because shader / texture
  ///       creation requires a live GL context.
  explicit streamer(config conf = {});

  /// Anchors the world at a geographic coordinate: derives the anchor
  /// tile, tile size, and origin offset from `latitude` / `longitude`
  /// (degrees), overriding those fields of `conf.world`.
  /// @note A raylib window must already be initialized (`InitWindow`)
  ///       before constructing a streamer because shader / texture
  ///       creation requires a live GL context.
  streamer(double latitude, double longitude, config conf = {});

  ~streamer();

  streamer(const streamer&) = delete;

  streamer& operator=(const streamer&) = delete;

  streamer(streamer&&) noexcept;

  streamer& operator=(streamer&&) noexcept;

  /// Updates the desired tile set based on the camera and promotes any
  /// finished downloads into renderable GPU resources. Cheap to call
  /// every frame.
  ///
  /// Caches `camera` and `world_offset` for use by the matching `draw()`
  /// and `ground_height()` calls. Call once per frame, after applying
  /// any large-world rebase to your scene.
  ///
  /// @param camera        Camera in user space (i.e. `camera.position`
  ///                      is whatever frame your game uses; typically
  ///                      near the origin to keep float precision tight).
  /// @param world_offset  Vector that maps user space to absolute world
  ///                      space via `absolute = user - offset`. Default
  ///                      `{0,0,0}` means user space *is* absolute space
  ///                      (no shifting).
  void update(const Camera3D& camera, Vector3 world_offset = {0, 0, 0}) const;

  /// Renders all currently loaded tiles in view. Must be called between
  /// `BeginMode3D` / `EndMode3D` after `update()` in the same frame.
  void draw();

  /// Draws 3D tile bounds. Call inside the same `BeginMode3D` block as
  /// `draw()`.
  void draw_debug_3d();

  /// Draws 2D zoom labels above the tiles (green = desired, red =
  /// resident but no longer desired). Call after `EndMode3D`.
  void draw_debug_labels();

  /// Return true for initial loading only.
  [[nodiscard]] bool is_loading() const;

  /// Fraction of the desired tile set that is loaded, in [0, 1].
  /// Monotonic during the initial load; pair with `is_loading()` to
  /// drive a splash screen.
  [[nodiscard]] float loading_progress() const;

  /// A sensible initial camera position over the world anchor, raised by
  /// `altitude` meters. With the lat/lon constructor this is exactly
  /// over the requested coordinate.
  [[nodiscard]] Vector3 initial_position(float altitude) const;

  /// Returns the terrain altitude (Y world-coordinate) under `position`,
  /// bilinearly sampled from the resident tile's height grid at the
  /// equivalent UV (integer-meter sample resolution: ±0.5 m).
  /// @param position     Query point in user space. Internally combined
  ///                     with the `world_offset` cached by the most
  ///                     recent `update()`.
  /// @returns The altitude, or `nullopt` if no loaded tile covers the
  ///          queried XZ point. Callers should generally fall back to a
  ///          previous frame's value or to 0 when nullopt is returned.
  /// @note O(1): a direct read from a CPU-resident height grid.
  [[nodiscard]] std::optional<float> ground_height(Vector3 position) const;

  /// @name Rendering parameter setters
  /// Safe to call any time after construction; take effect immediately.
  /// @{

  /// Pushes a whole rendering configuration to the shader in one call.
  void set_rendering(const rendering_config& conf);

  /// Sets the fog color for distance attenuation. Match this to your sky
  /// color for a seamless horizon.
  void set_fog_color(Color color);

  /// Sets the distance at which colors begin to blend with the fog.
  void set_fog_start(float distance);

  /// Sets the distance at which colors are fully blended with the fog.
  void set_fog_end(float distance);

  /// Sets the ambient light color. Use this to drive day / night /
  /// weather lighting changes.
  void set_ambient_light(Color color);

  /// Sets the sun direction vector used by the lighting calculations.
  void set_sun_direction(Vector3 direction);

  /// Sets the sun lighting intensity (contrast between lit and shaded
  /// areas).
  void set_sun_scale(float scale);

  /// Sets the heightmap scale factor (drama factor).
  void set_height_scale(float scale);

  /// Sets the normals scale factor (lighting contrast).
  void set_normals_scale(float scale);

  /// @}

 private:
  struct impl;
  std::unique_ptr<impl> impl_;
};
}  // namespace raytiles

#endif  // RAYTILES_LIBRARY_H
