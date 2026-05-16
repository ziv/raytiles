/// @file raytiles.h
/// Public API for the raytiles library: stream a 3D world built from satellite
/// imagery and heightmap tiles around a moving camera and render it via raylib.
#ifndef RAYTILES_LIBRARY_H
#define RAYTILES_LIBRARY_H
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "raylib.h"
#include "detail/raii.hpp"
#include "detail/tile.hpp"
#include "detail/utils.hpp"

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
        /// and `base_zoom_tile_size`. Note: this library has never been tested
        /// with a `base_zoom` lower than 9.
        int base_zoom = 9;

        /// Highest level-of-detail zoom available. Tiles directly under the camera
        /// are subdivided up to this zoom.
        /// Changing this value also requires updating `streaming_config::thresholds`.
        /// 15 is the maximum zoom currently supported.
        int max_zoom = 15;

        /// World size (in meters) of one tile at `base_zoom`. Tiles at higher zooms
        /// are scaled by `1 / (1 << (zoom - base_zoom))`.
        float base_zoom_tile_size = 66400.0f;

        /// Skirt geometry overlap factor (per side) used to hide cracks between
        /// neighboring tiles at different LODs. Expressed relative to a tile at
        /// `max_zoom`. Baked into generated meshes.
        float skirt_size = 0.01f;

        /// Vertical drop (in meters) of the skirt geometry below each tile's edge.
        /// Larger values hide cracks more reliably but cost more fill rate. Baked
        /// into generated meshes.
        Meters skirt_drop = 1000.0f;

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
        /// the resident tile count under 600. If the zoom range changes, this
        /// map must be updated to match.
        std::unordered_map<Zoom, Meters> thresholds = {
            {9, 100000.0f},
            {10, 80000.0f},
            {11, 40000.0f},
            {12, 20000.0f},
            {13, 10000.0f},
            {14, 5000.0f},
            {15, 2500.0f}
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
    };

    /// Rendering / shader-uniform parameters. Every field here is genuinely
    /// runtime-mutable; most have matching `streamer::set_*` setters that push
    /// new values to the shader on the next `update()`.
    struct rendering_config {
        /// Near clip plane (meters) used by the displacement shader for fog and
        /// depth-precision tuning. Match this to your camera setup.
        MetersD near_plane = 1;

        /// Far clip plane (meters) used by the displacement shader for fog and
        /// depth-precision tuning. Match this to your camera setup.
        MetersD far_plane = 400000;

        /// Distance (in meters) at which atmospheric fog starts to fade tiles to
        /// `fog_color`.
        Meters fog_start = 100000.0f;

        /// Distance (in meters) at which fog reaches full cover.
        Meters fog_end = 150000.0f;

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

    // forward-declared so the public header doesn't drag httplib in via
    // downloader.hpp. defined in src/downloader.hpp.
    class pool;

    class renderer {
    public:
        explicit renderer(rendering_config &conf);

        int draw(const Vector3 &position, const DebugView &draw_view);

        /// Draws a 2D HUD with streamer statistics (loaded / loading counts, etc.)
        /// and zoom labels above the tiles
        /// Call between `BeginDrawing` / `EndDrawing`, after `EndMode3D`.
        static void debug(const Camera3D &camera, const DebugView &draw_view);

        /// Draws 3D debug overlays (tile bounds). Call inside the same
        /// `BeginMode3D` / `EndMode3D` block as `draw`.
        static void debug_3d(const DebugView &draw_view);

        /// Sets the ambient light color sent to the displacement shader. Use this
        /// to drive day / night / weather lighting changes.
        void set_ambient_light(Color color);

        /// Sets the ambient light color sent to the displacement shader. Use this
        /// to drive day / night / weather lighting changes.
        void set_ambient_light(Vector4 color);

        /// Sets the ambient light color sent to the displacement shader. Use this
        /// to drive day / night / weather lighting changes.
        void set_ambient_light(float r, float g, float b, float a);

        /// Sets the fog color for distance attenuation. Match this to your sky
        /// color for a seamless horizon.
        void set_fog_color(Color color);

        /// Sets the fog color for distance attenuation. Match this to your sky
        /// color for a seamless horizon.
        void set_fog_color(Vector4 color);

        /// Sets the fog color for distance attenuation. Match this to your sky
        /// color for a seamless horizon.
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

    private:
        void update_shader_uniforms();

        rendering_config &rendering;

        raii::shader displacement_shader;
        raii::material material{};

        // shaders slots locations
        int cam_pos_loc = -1;
        int ambient_loc = -1;
        int fog_color_loc = -1;
        int tex_albedo_loc = -1;
        int tex_height_loc = -1;
        int tex_normal_loc = -1;
        int sun_dir_loc = -1;
        int sun_scale_loc = -1;
        int height_scale_loc = -1;
        int normal_scale_loc = -1;
        int fog_start_loc = -1;
        int fog_end_loc = -1;
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
        explicit streamer(const world_config &world_conf = {},
                          const streaming_config &streaming_conf = {},
                          const rendering_config &rendering_conf = {},
                          const pool_config &pool_conf = {});

        ~streamer();

        streamer(const streamer &) = delete;

        streamer &operator=(const streamer &) = delete;

        streamer(streamer &&) noexcept;

        /// Updates the desired tile set based on the camera and promotes any
        /// finished downloads into renderable GPU resources. Cheap to call every
        /// frame; internally rate-limited by `streaming_config::upload_budget_sec`
        /// and `streaming_config::max_uploads_per_frame`.
        void update(const Camera3D &camera);

        /// Renders all currently loaded tiles. Must be called between
        /// `BeginMode3D` / `EndMode3D` with the same camera passed to `update`.
        void draw(const Camera3D &camera);

        /// Draws a 2D HUD with streamer statistics (loaded / loading counts, etc.).
        /// Call between `BeginDrawing` / `EndDrawing`, after `EndMode3D`.
        void debug(const Camera3D &camera);

        /// Draws 3D debug overlays (tile bounds, LOD seams). Call inside the same
        /// `BeginMode3D` / `EndMode3D` block as `draw`.
        void debug_3d();

        /// Returns the underlying renderer instance for direct access
        /// to shader parameters and debug methods.
        renderer &get_renderer();

        /// Returns the terrain altitude (Y world-coordinate) under `position`,
        /// sampled from the heightmap pixel at the equivalent UV.
        /// @returns The altitude, or `nullopt` if no loaded tile covers the
        ///          queried XZ point. Callers should generally fall back to a
        ///          previous frame's value or to 0 when nullopt is returned.
        /// @note Each loaded tile keeps its decoded heightmap in CPU RAM (~192KB)
        ///       so this query is a direct pixel read; cost is O(1).
        [[nodiscard]] std::optional<float> ground_height(Vector3 position) const;

    private:
        void build_required(Zoom zoom, int tx, int tz, float render_radius_sq);

        void process_loaded_tiles();

        void process_current_location();

        void remove_unused_tiles();

        [[nodiscard]] loading_tile spawn(const tile_key &tile) const;

        [[nodiscard]] MetersSq calculate_horizon() const;

        [[nodiscard]] bool is_tile_covered(const tile_key &key) const;

        [[nodiscard]] bool is_tile_out_of_area(const tile_key &key) const;

        // configuration
        world_config world;
        streaming_config streaming;
        rendering_config rendering;

        // exposed in the public header (part of the API)
        renderer tile_renderer;
        // held by unique_ptr so the public header can forward-declare `pool`
        // and keep httplib out of every consumer's translation unit.
        std::unique_ptr<pool> tile_downloader;

        // internal cache
        int rendered = 0;
        float width = 0.0f;
        float height = 0.0f;

        // update every frame
        Vector3 last_position = {-9999.9f, -9999.9f, -9999.9f};
        Frustum last_frustum{};

        // desired_keys is updated under condition
        // the maps auto built/evicted every frame
        std::unordered_set<tile_key> desired_keys;
        std::unordered_map<tile_key, loading_tile> loading_tiles;
        std::unordered_map<tile_key, loaded_tile> rendering_tiles;

        // metadata about tiles by their zoom
        std::unordered_map<Zoom, tile_value> tiles;
    };
} // namespace raytiles

#endif  // RAYTILES_LIBRARY_H
