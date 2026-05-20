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
    using MetersD = double;
    using MetersSq = double;

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

    /// Configuration for the background tile download pool. Passed by value to
    /// the `streamer` constructor.
    struct pool_config {
        /// Number of background download workers. Downloads are I/O-bound so it's
        /// safe to use more threads than CPU cores; 2 is a reasonable default for
        /// HTTP keep-alive against a single host.
        int download_threads = 4;

        /// Skip TLS certificate verification for tile downloads. Only useful for
        /// local proxies; never enable against a real server.
        bool allow_insecure_tls = false;

        /// Whether the pool's worker threads emit log lines.
        bool use_logger = false;

        /// On-disk cache path templates, formatted with `{zoom}/{x}/{z}` via
        /// `std::vformat`. Parent directories are created on demand.
        std::string texture_cache_path = "assets/texture/{}/{}/{}.png";
        std::string heightmap_cache_path = "assets/heightmap/{}/{}/{}.png";
        std::string normals_cache_path = "assets/normals/{}/{}/{}.png";

        /// Provider URL templates. The full request URL is constructed from
        /// `{zoom}/{x}/{z}` (plus any optional token in the template). Any
        /// provider following the XYZ (slippy-map) convention works, as long as
        /// the heightmap provider returns RGB-encoded heightmaps.
        std::string texture_url = RAYTILES_TEXTURE_URL;
        std::string texture_host{};
        std::string texture_url_path{};

        std::string heightmap_url = RAYTILES_HEIGHTMAP_URL;
        std::string heightmap_host{};
        std::string heightmap_url_path{};

        std::string normals_url = RAYTILES_NORMALS_URL;
        std::string normals_host{};
        std::string normals_url_path{};
    };
}

namespace raytiles {
    class renderer;
    class tiles_manager;

    /// World topology / geometry parameters. Everything in this struct is
    /// effectively immutable once a `streamer` exists: changing any field
    /// requires rebuilding meshes, re-uploading textures, or re-anchoring the
    /// world. Set once at construction.
    struct world_config {
        /// World-space anchor in tile coordinates at `base_zoom`. The streamer
        /// translates tile XY to world XZ relative to this anchor so the world
        /// origin is wherever you want it (e.g. your runway).
        int anchor_x_tile = 306;
        int anchor_z_tile = 207;

        /// Lowest level-of-detail zoom that will ever be loaded. Tiles outside the
        /// camera's near radius are kept at this zoom to bound the working set.
        /// Changing this value also requires updating `streaming_config::thresholds`
        /// and `base_zoom_tile_size`. Must be `>= min_supported_zoom`.
        int base_zoom = min_supported_zoom;

        /// Highest level-of-detail zoom available. Tiles directly under the camera
        /// are subdivided up to this zoom.
        /// Changing this value also requires updating `streaming_config::thresholds`.
        /// Must be `<= max_supported_zoom` and `>= base_zoom`.
        int max_zoom = max_supported_zoom;

        /// World size (in meters) of one tile at `base_zoom`. Tiles at higher zooms
        /// are scaled by `1 / (1 << (zoom - base_zoom))`.
        float base_zoom_tile_size = 66400.0f;

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

        /// Whether to emit log lines from the streamer. Logs from the main
        /// thread/process are routed through raylib's `TraceLog`.
        bool use_logger = false;
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

        /// Squared XZ distance the camera must travel before the desired-tile set
        /// is recomputed. Keep this large enough that small movements don't churn
        /// the working set.
        MetersSq update_distance_sq = 1000.0f * 1000.0f;

        /// Altitude delta (in meters) that triggers a desired-set recomputation,
        /// independent of `update_distance_sq`. Lets you stream new LODs as you
        /// climb or descend without horizontal motion.
        Meters update_height = 500.0f;

        /// Wall-clock budget (in seconds) per frame for promoting downloaded tiles
        /// into GPU resources. Caps the cost of a single bursty frame.
        double upload_budget_sec = 0.002;

        /// Hard cap on tile promotions per frame, on top of `upload_budget_sec`.
        /// Whichever limit is hit first stops the loop.
        int max_uploads_per_frame = 8;

        /// Near clip plane (meters) used by the displacement shader for fog and
        /// depth-precision tuning. Match this to your camera setup.
        MetersD near_plane = 1;

        /// Far clip plane (meters) used by the displacement shader for fog and
        /// depth-precision tuning. Match this to your camera setup.
        MetersD far_plane = 400000;
    };

    /// Rendering / shader-uniform parameters. Every field here is genuinely
    /// runtime-mutable; most have matching `streamer::set_*` setters that push
    /// new values to the shader on the next `update()`.
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

    /// Per-frame driver that maintains the working set of tiles around a camera
    /// and renders them. One streamer manages one world; create more if you need
    /// independent worlds.
    ///
    /// Typical use:
    /// @code
    ///   raytiles::streamer s(world, streaming, rendering, pool_conf);
    ///   while (!WindowShouldClose()) {
    ///     s.update(camera);
    ///     BeginDrawing();
    ///     BeginMode3D(camera);
    ///     s.draw(camera);
    ///     EndMode3D();
    ///     EndDrawing();
    ///   }
    /// @endcode
    ///
    /// All raylib resources are owned via RAII; destruction is safe and complete.
    /// Movable but not copyable.
    class streamer {
    public:
        /// @param world_conf
        /// @param streaming_conf
        /// @param rendering_conf
        /// @param pool_conf Tunable parameters for the tile downloader pool; moved into the pool.
        /// @note A raylib window must already be initialized (`InitWindow`) before
        ///       constructing a streamer because shader / texture creation requires
        ///       a live GL context.
        explicit streamer(world_config world_conf = {},
                          streaming_config streaming_conf = {},
                          rendering_config rendering_conf = {},
                          pool_config pool_conf = {});

        ~streamer();

        streamer(const streamer &) = delete;

        streamer &operator=(const streamer &) = delete;

        streamer(streamer &&) noexcept;

        streamer &operator=(streamer &&) noexcept;

        /// Updates the desired tile set based on the camera and promotes any
        /// finished downloads into renderable GPU resources. Cheap to call every
        /// frame; internally rate-limited by `streaming_config::upload_budget_sec`
        /// and `streaming_config::max_uploads_per_frame`.
        void update(const Camera3D &camera);

        /// Renders all currently loaded tiles in view. Must be called between
        /// `BeginMode3D` / `EndMode3D` with the same camera passed to `update`.
        void draw(const Camera3D &camera);

        /// Return true for initial loading only
        [[nodiscard]] bool is_loading() const;

        /// Return loading percents
        [[nodiscard]] float get_loading() const;

        /// Returns the terrain altitude (Y world-coordinate) under `position`,
        /// sampled from the heightmap pixel at the equivalent UV.
        /// @returns The altitude, or `nullopt` if no loaded tile covers the
        ///          queried XZ point. Callers should generally fall back to a
        ///          previous frame's value or to 0 when nullopt is returned.
        /// @note Each loaded tile keeps its decoded heightmap in CPU RAM (~192KB)
        ///       so this query is a direct pixel read; cost is O(1).
        [[nodiscard]] std::optional<float> ground_height(Vector3 position) const;

        /// @name Shader parameter setters
        /// Forwarded onto the internal renderer; safe to call any time after
        /// construction. Take effect on the next `update()`.
        /// @{

        /// Sets the ambient light color sent to the displacement shader. Use this
        /// to drive day / night / weather lighting changes.
        void set_ambient_light(Color color);

        void set_ambient_light(Vector4 color);

        void set_ambient_light(float r, float g, float b, float a);

        /// Sets the fog color for distance attenuation. Match this to your sky
        /// color for a seamless horizon.
        void set_fog_color(Color color);

        void set_fog_color(Vector4 color);

        void set_fog_color(float r, float g, float b, float a);

        /// Sets the fog start distance — the distance from the camera at which
        /// colors begin to blend with the fog.
        void set_fog_start(float distance);

        /// Sets the fog end distance — the distance from the camera at which
        /// colors are fully blended with the fog color.
        void set_fog_end(float distance);

        /// Sets the heightmap scale factor, which exaggerates or flattens the
        /// terrain relief (drama factor).
        void set_height_scale(float scale);

        /// Sets the normals scale factor to increase or reduce lighting contrast.
        void set_normals_scale(float scale);

        /// Sets the sun direction vector used by the displacement shader's
        /// lighting calculations.
        void set_sun_direction(Vector3 direction);

        /// Sets the sun lighting intensity, which controls the contrast between
        /// lit and shaded areas.
        void set_sun_scale(float scale);

        /// @}

    private:
        // streamer keeps only the streaming-policy bits it actually uses
        // (update gating, near/far for frustum extraction). All tile
        // lifecycle state lives in `tile_manager`.
        streaming_config streaming;

        std::unique_ptr<renderer> tile_renderer;
        std::unique_ptr<tiles_manager> tile_manager;

        int rendered = 0;

        // update every frame
        Vector3 last_position = {-9999.9f, -9999.9f, -9999.9f};
        Frustum last_frustum{};
    };
} // namespace raytiles

#endif  // RAYTILES_LIBRARY_H
