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
    /// `max_zoom` throws `std::runtime_error`.
    constexpr Zoom max_supported_zoom = 15;

    /// Number of zoom levels in `[min_supported_zoom, max_supported_zoom]`.
    /// Sizes the per-zoom arrays `world_config::skirt_overlap` and
    /// `streaming_config::thresholds`. Slot `i` corresponds to zoom
    /// `base_zoom + i`; slots beyond `max_zoom - base_zoom` are unused.
    constexpr std::size_t zoom_levels = max_supported_zoom - min_supported_zoom + 1;

    /// A single plane in world space, used for frustum culling. `normal` points
    /// into the volume the plane bounds; `distance` is the plane's offset from
    /// origin along that normal.
    struct Plane {
        Vector3 normal;
        Meters distance;
    };

    /// Six-plane view frustum (left/right/bottom/top/near/far).
    struct Frustum {
        Plane planes[6];
    };

    /// Tile download / provider configuration: worker threads, provider URL
    /// templates, and the on-disk cache layout.
    struct network_config {
        /// Number of background download workers. Downloads are I/O-bound so it's
        /// safe to use more threads than CPU cores; 4 is a reasonable default for
        /// HTTP keep-alive against a small set of hosts.
        int download_threads = 4;

        /// Skip TLS certificate verification for tile downloads. Only useful for
        /// local proxies; never enable against a real server.
        bool allow_insecure_tls = false;

        /// On-disk cache path templates, formatted with `{zoom}/{x}/{z}` via
        /// `std::vformat`. Parent directories are created on demand.
        std::string texture_cache_path = ".cache/texture/{}/{}/{}.png";
        std::string heightmap_cache_path = ".cache/heightmap/{}/{}/{}.png";
        std::string normals_cache_path = ".cache/normals/{}/{}/{}.png";

        /// Provider URL templates using `:zoom:` / `:x:` / `:y:` tokens
        /// (substituted at request time). Any provider following the XYZ
        /// (slippy-map) convention works, as long as the heightmap provider
        /// returns Terrarium RGB-encoded heightmaps (`ground_height()`
        /// depends on that encoding).
        std::string texture_url = RAYTILES_TEXTURE_URL;
        std::string heightmap_url = RAYTILES_HEIGHTMAP_URL;
        std::string normals_url = RAYTILES_NORMALS_URL;
    };

    /// World topology / geometry parameters. Everything in this struct is
    /// effectively immutable once a `streamer` exists: changing any field
    /// requires rebuilding meshes, re-uploading textures, or re-anchoring the
    /// world. Set once at construction.
    struct world_config {
        /// World-space anchor in tile coordinates at `base_zoom`. The streamer
        /// translates tile XY to world XZ relative to this anchor so the world
        /// origin is wherever you want it (e.g. your runway). Prefer the
        /// latitude/longitude constructor, which computes the anchor for you.
        int anchor_x_tile = 306;
        int anchor_z_tile = 207;

        /// Lowest level-of-detail zoom that will ever be loaded. Tiles outside the
        /// camera's near radius are kept at this zoom to bound the working set.
        /// Changing this value also requires updating `streaming_config::thresholds`
        /// and `base_zoom_tile_size`. Must be `>= min_supported_zoom`.
        Zoom base_zoom = min_supported_zoom;

        /// Highest level-of-detail zoom available. Tiles directly under the camera
        /// are subdivided up to this zoom.
        /// Changing this value also requires updating `streaming_config::thresholds`.
        /// Must be `<= max_supported_zoom` and `>= base_zoom`.
        Zoom max_zoom = max_supported_zoom;

        /// World size (in meters) of one tile at `base_zoom`. Tiles at higher zooms
        /// are scaled by `1 / (1 << (zoom - base_zoom))`.
        Meters base_zoom_tile_size = 66400.0f;

        /// Per-zoom skirt overlap factors, allowing you to tweak the amount of overlap
        /// (and thus fill rate) at different zoom levels. Baked into generated meshes.
        /// Indexed as `skirt_overlap[zoom - base_zoom]`, so slot `i` applies to zoom
        /// `base_zoom + i`. Only slots `[0, max_zoom - base_zoom]` are read; trailing
        /// slots are ignored.
        std::array<float, zoom_levels> skirt_overlap = {
            1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f
        };

        /// Generate trilinear / anisotropic mipmaps for the albedo texture on
        /// upload. Strongly recommended; avoids shimmering at distance.
        bool use_mipmap = true;

        /// Initial-position hint returned by `streamer::get_initial_position`:
        /// the world-space point corresponding to the anchor. The
        /// latitude/longitude constructor fills it in so the camera can start
        /// exactly over the requested coordinates; with a manual anchor it is
        /// yours to set (or leave zero to start at the anchor tile's corner).
        Vector3 offset = {0.0f, 0.0f, 0.0f};
    };

    /// Tile-streaming parameters. Governs *which* tiles are kept resident and
    /// how aggressively the working set is updated. Safe to tweak at runtime
    /// (no mesh / texture rebuild), but most users set it once.
    struct streaming_config {
        /// Radius, in `world_config::base_zoom` tiles, of the disc of tiles
        /// loaded around the camera. Larger values = more tiles in flight =
        /// more memory / bandwidth.
        int rendering_radius = 6;

        /// Per-zoom distance thresholds (covering `world_config::base_zoom`
        /// through `world_config::max_zoom`). Tuned for performance and to keep
        /// the resident tile count under 600. Indexed as
        /// `thresholds[zoom - base_zoom]`, so slot `i` applies to zoom
        /// `base_zoom + i`. Only slots `[0, max_zoom - base_zoom]` are read;
        /// trailing slots are ignored.
        std::array<Meters, zoom_levels> thresholds = {
            100000.0f, 80000.0f, 40000.0f, 20000.0f, 10000.0f, 5000.0f, 2500.0f
        };

        /// Distance (in meters) the camera must travel to trigger a
        /// desired-set recomputation. Keep this large enough that small
        /// movements don't churn the working set.
        Meters update_distance = 500.0f;

        /// Wall-clock budget (in seconds) per frame for promoting downloaded tiles
        /// into GPU resources. Caps the cost of a single bursty frame.
        double upload_budget_sec = 0.002;

        /// Hard cap on tile promotions per frame, on top of `upload_budget_sec`.
        /// Whichever limit is hit first stops the loop.
        int max_uploads_per_frame = 8;

        /// Near clip plane (meters) used when extracting the culling frustum.
        /// Match this to your camera setup.
        MetersD near_plane = 1;

        /// Far clip plane (meters) used when extracting the culling frustum.
        /// Match this to your camera setup.
        MetersD far_plane = 400000;
    };

    /// Rendering / shader-uniform parameters. Every field here is genuinely
    /// runtime-mutable; most have matching `streamer::set_*` setters that push
    /// new values to the shader immediately. Colors are float RGBA (0..1) so
    /// initial values keep full precision; the runtime setters take raylib
    /// `Color` for convenience.
    struct rendering_config {
        /// Distance (in meters) at which atmospheric fog starts to fade tiles to
        /// `fog_color`.
        Meters fog_start = 100000.0f;

        /// Distance (in meters) at which fog reaches full cover.
        Meters fog_end = 150000.0f;

        /// Vertical drop (in meters) of the skirt geometry below each tile's edge.
        /// Larger values hide cracks more reliably but cost more fill rate.
        /// Baked into shader. 0 disable this feature.
        Meters skirt_drop = 0.0f;

        /// Fog color (RGBA, 0..1). Match this to your sky color for a seamless
        /// horizon.
        float fog_color[4] = {0.0f, 0.0f, 1.0f, 1.0f};

        /// World ambient color (RGBA, 0..1). Drives day / night / weather
        /// lighting changes.
        float ambient_light[4] = {1.0f, 1.0f, 1.0f, 1.0f};

        /// Sun direction vector. The shader normalizes it internally; magnitude
        /// is irrelevant.
        float sun_direction[3] = {0.1f, 1.0f, 0.1f};

        /// Sun lighting intensity, controlling contrast between lit and shaded
        /// areas.
        float sun_scale = 1.0f;

        /// Scales the heightmap by this factor to exaggerate or flatten the
        /// terrain relief (drama factor).
        float height_scale = 1.0f;

        /// Scales the normals by this factor to increase or reduce lighting
        /// contrast. Higher values make the terrain look bumpier, but can cause
        /// lighting artifacts if the normals become too steep.
        float normals_scale = 1.0f;
    };

    /// Complete streamer configuration. Every field of every nested struct has
    /// a sensible default, so `{}` is a fully working configuration and call
    /// sites only spell out what they change:
    /// @code
    ///   raytiles::streamer s(lat, lon, {.streaming = {.rendering_radius = 8}});
    /// @endcode
    struct config {
        world_config world{};
        streaming_config streaming{};
        rendering_config rendering{};
        network_config network{};
    };

    class terrain_renderer;
    class tile_source;
    class tile_store;

    /// Per-frame driver that maintains the working set of tiles around a camera
    /// and renders them. One streamer manages one world; create more if you need
    /// independent worlds.
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
    /// All raylib resources are owned via RAII; destruction is safe and complete.
    /// Movable but not copyable.
    class streamer {
    public:
        /// Constructs a streamer. `config{}` (the default) is fully usable —
        /// free Esri imagery + Mapzen terrain around the default anchor.
        /// @param cfg complete configuration; see the `config` nested structs.
        /// @note A raylib window must already be initialized (`InitWindow`) before
        ///       constructing a streamer because shader / texture creation requires
        ///       a live GL context.
        /// @throws std::runtime_error when `cfg.world` violates the zoom bounds.
        explicit streamer(config cfg = {});

        /// Constructs a streamer anchored at a geographic coordinate: the tile
        /// anchor, tile size, and initial-position offset of `cfg.world` are
        /// computed from `latitude` / `longitude` (degrees); everything else in
        /// `cfg` is used as-is.
        /// @note A raylib window must already be initialized (`InitWindow`) before
        ///       constructing a streamer because shader / texture creation requires
        ///       a live GL context.
        /// @throws std::runtime_error when `cfg.world` violates the zoom bounds.
        streamer(double latitude, double longitude, config cfg = {});

        ~streamer();

        streamer(const streamer &) = delete;

        streamer &operator=(const streamer &) = delete;

        streamer(streamer &&) noexcept;

        streamer &operator=(streamer &&) noexcept;

        /// Updates the desired tile set based on the camera and promotes any
        /// finished downloads into renderable GPU resources. Cheap to call every
        /// frame.
        ///
        /// Caches `camera` and `world_offset` for use by the matching `draw()`
        /// and `ground_height()` calls. Call once per frame, after applying any
        /// large-world rebase to your scene, before any `draw()` /
        /// `ground_height()` calls.
        ///
        /// @param camera        Camera in user space (i.e. `camera.position` is
        ///                      whatever frame your game uses; typically near
        ///                      the origin to keep float precision tight).
        /// @param world_offset  Vector that maps user space to absolute world
        ///                      space via `absolute = user - offset`. Default
        ///                      `{0,0,0}` means user space *is* absolute space
        ///                      (no shifting).
        void update(const Camera3D &camera, Vector3 world_offset = {0, 0, 0});

        /// Renders all currently loaded tiles in view. Must be called between
        /// `BeginMode3D` / `EndMode3D` after `update()` in the same frame.
        /// Reuses the camera and `world_offset` cached by `update()`.
        void draw();

        /// Draws tile-bound wireframes for debugging. Call inside the same
        /// `BeginMode3D` / `EndMode3D` block as `draw()`.
        void draw_debug_3d();

        /// Draws per-tile zoom labels for debugging. Call after `EndMode3D`.
        void draw_debug_labels();

        /// True until the initial tile set finished loading. Use together with
        /// `loading_progress()` to drive a splash screen.
        [[nodiscard]] bool is_loading() const;

        /// Initial-load progress in `[0, 1]`, monotonically non-decreasing;
        /// returns 1 once loading completed.
        [[nodiscard]] float loading_progress() const;

        /// A sensible starting camera position: the world-space point of the
        /// constructor's anchor (see `world_config::offset`), raised `y` meters.
        [[nodiscard]] Vector3 get_initial_position(float y) const;

        /// Returns the terrain altitude (Y world-coordinate) under `position`,
        /// sampled from the heightmap pixel at the equivalent UV.
        /// @param position     Query point in user space. Internally combined
        ///                     with the `world_offset` cached by the most
        ///                     recent `update()` to recover absolute coords
        ///                     for the tile lookup.
        /// @returns The altitude, or `nullopt` if no loaded tile covers the
        ///          queried XZ point. Callers should generally fall back to a
        ///          previous frame's value or to 0 when nullopt is returned.
        /// @note Each loaded tile keeps its decoded heightmap in CPU RAM (~192KB)
        ///       so this query is a direct pixel read; cost is O(1).
        [[nodiscard]] std::optional<float> ground_height(Vector3 position) const;

        /// @name Shader parameter setters
        /// Push new uniform values to the displacement shader; safe to call any
        /// time after construction, take effect on the next drawn frame. For
        /// float-precision colors set the initial values in
        /// `config::rendering` instead.
        /// @{

        /// Sets the ambient light color. Use this to drive day / night /
        /// weather lighting changes.
        void set_ambient_light(Color color) const;

        /// Sets the fog color for distance attenuation. Match this to your sky
        /// color for a seamless horizon.
        void set_fog_color(Color color) const;

        /// Sets fog color and both fade distances in one call: colors start
        /// blending at `start` meters from the camera and are fully replaced
        /// by `color` at `end` meters.
        void set_fog(Color color, float start, float end) const;

        /// Sets the sun: `direction` is normalized by the shader (magnitude
        /// irrelevant); `intensity` controls the contrast between lit and
        /// shaded areas (default 1.0).
        void set_sun(Vector3 direction, float intensity) const;

        /// Sets the heightmap scale factor, which exaggerates or flattens the
        /// terrain relief (drama factor).
        void set_height_scale(float scale) const;

        /// Sets the normals scale factor to increase or reduce lighting contrast.
        void set_normals_scale(float scale) const;

        /// @}

    private:
        // streamer keeps only the streaming-policy bits it actually uses
        // (update gating, near/far for frustum extraction). All tile
        // lifecycle state lives in `tile_store`, all async download state in
        // `tile_source` — the streamer is just the per-frame orchestrator.
        float near_plane;
        float far_plane;
        float update_distance_sq;
        Vector3 init_position;

        // declaration order is construction order: the store validates the
        // config (and throws) before the renderer creates any GL resources
        std::unique_ptr<tile_source> source_;
        std::unique_ptr<tile_store> store_;
        std::unique_ptr<terrain_renderer> renderer_;

        int rendered = 0;

        // update every frame
        Vector3 last_position = {-9999.9f, -9999.9f, -9999.9f};
        Frustum last_frustum{};

        // Cached current-frame inputs from update(); read by draw() and
        // ground_height(). Convention: cached_camera_.position is in user
        // space, cached_world_offset_ maps user → absolute via
        // absolute = user - offset.
        Camera3D cached_camera_{};
        Vector3 cached_world_offset_ = {0.0f, 0.0f, 0.0f};
    };
} // namespace raytiles

#endif  // RAYTILES_LIBRARY_H
