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
#include "src/raii.hpp"
#include "src/tile.hpp"

#ifndef RAYTILES_TEXTURE_HOST
#define RAYTILES_TEXTURE_HOST "https://server.arcgisonline.com"
#endif

#ifndef RAYTILES_TEXTURE_URL_PATH
#define RAYTILES_TEXTURE_URL_PATH "/ArcGIS/rest/services/World_Imagery/MapServer/tile/{zoom}/{y}/{x}"
#endif

#ifndef RAYTILES_HEIGHTMAP_HOST
#define RAYTILES_HEIGHTMAP_HOST "https://s3.amazonaws.com"
#endif

#ifndef RAYTILES_HEIGHTMAP_URL_PATH
#define RAYTILES_HEIGHTMAP_URL_PATH "/elevation-tiles-prod/terrarium/{zoom}/{x}/{y}.png"
#endif

#ifndef RAYTILES_NORMALS_HOST
#define RAYTILES_NORMALS_HOST "https://s3.amazonaws.com"
#endif

#ifndef RAYTILES_NORMALS_URL_PATH
#define RAYTILES_NORMALS_URL_PATH "/elevation-tiles-prod/normal/{zoom}/{x}/{y}.png"
#endif


namespace raytiles {
    /// Tunable parameters for a `streamer` instance. Everything has a sensible
    /// default; override fields before constructing the streamer.
    ///
    /// All distances are in raylib world units (1 unit = 1 meter at base zoom in
    /// the default configuration).
    struct config {
        /// Lowest level-of-detail zoom that will ever be loaded. Tiles outside the
        /// camera's near radius are kept at this zoom to bound the working set.
        /// Changing this value must also update "thresholds" and "base_zoom_tile_size"
        /// Note that this library never tested against base_zoom lower than 9
        int base_zoom = 9;

        /// Highest level-of-detail zoom available. Tiles directly under the camera
        /// are subdivided up to this zoom.
        /// Changing this value must also update "thresholds"
        /// 15 is max zoom currently supported
        int max_zoom = 15;

        /// World size (in meters) of one tile at `base_zoom`. Tiles at higher zooms
        /// are scaled by `1 / (1 << (zoom - base_zoom))`.
        float base_zoom_tile_size = 66400.0f;

        /// Radius, in `base_zoom` tiles, of the disc of tiles loaded around the
        /// camera. Larger values = more tiles in flight = more memory / bandwidth.
        int rendering_radius = 6;

        /// Skirt geometry overlap factor (per side) used to hide cracks between
        /// neighboring tiles at different LODs.
        /// Refer to the max_zoom.
        float skirt_size = 0.01f;

        /// Scaling the heightmap by this factor to increase or reduce the real height
        /// into desired (drama factor)
        float height_scale = 1.0f;

        /// Scaling the normals by this factor to increase or reduce the lighting contrast.
        /// Higher values make the terrain look bumpier, but can cause lighting artifacts if the normals are too steep.
        float normals_scale = 1.0f;

        /// Squared XZ distance the camera must travel before the desired-tile set
        /// is recomputed. Keep this large enough that small movements don't churn
        /// the working set.
        float update_distance = 1000.0f * 1000.0f;

        /// Altitude delta (in meters) that triggers a desired-set recomputation,
        /// independent of `update_distance`. Lets you stream new LODs as you climb
        /// or descend without horizontal motion.
        float update_height = 500.0f;

        /// Wall-clock budget (in seconds) per frame for promoting downloaded tiles
        /// into GPU resources. Caps the cost of a single bursty frame.
        double upload_budget_sec = 0.002;

        /// Hard cap on tile promotions per frame, on top of `upload_budget_sec`.
        /// Whichever limit is hit first stops the loop.
        int max_uploads_per_frame = 8;

        /// World-space anchor in tile coordinates at `base_zoom`. The streamer
        /// translates tile XY to world XZ relative to this anchor so the world
        /// origin is wherever you want it (e.g. your runway).
        int anchor_x_tile = 306;
        int anchor_z_tile = 207;

        /// Near / far clip planes used by the displacement shader for fog and
        /// depth precision tuning. Match these to your camera setup.
        double near_plane = 1;
        double far_plane = 400000;

        /// Generate trilinear / anisotropic mipmaps for the albedo texture on
        /// upload. Strongly recommended; avoids shimmering at distance.
        bool use_mipmap = true;

        /// Vertical drop (in meters) of the skirt geometry below each tile's edge.
        /// Larger values hide cracks more reliably but cost more fill rate.
        float skirt_drop = 1000.0f;

        /// Distance (in meters) at which atmospheric fog starts to fade tiles to
        /// `set_fog_color`'s color.
        float fog_start = 100000.0f;

        /// Distance (in meters) at which fog reaches full cover.
        float fog_end = 150000.0f;

        /// Weather to log from the threads or from the main process
        /// Logging from main thread/process is done via raylib's TraceLog function
        bool use_logger = false;

        /// Zoom level distance thresholds (10 to 15)
        /// Optimized for performance and limit the tiles number under 600
        /// Changing base_zoom or max_zoom must be reflected here
        std::unordered_map<int, float> thresholds = {
            {9, 100000.0f},
            {10, 80000.0f},
            {11, 40000.0f},
            {12, 20000.0f},
            {13, 10000.0f},
            {14, 5000.0f},
            {15, 2500.0f}
        };
    };

    struct pool_config {
        /// Number of background download workers. Downloads are I/O-bound so it's
        /// safe to use more threads than CPU cores; 2 is a reasonable default for
        /// HTTP keep-alive against a single host.
        int download_threads = 2;

        /// Skip TLS certificate verification for tile downloads. Only useful for
        /// local proxies; never enable against a real server.
        bool allow_insecure_tls = false;

        /// Weather to log from the pool threads
        bool use_logger = false;

        /// On-disk cache path templates, formatted with `{zoom}/{x}/{z}` via
        /// `std::vformat`. Parent directories are created on demand.
        std::string texture_cache_path = "assets/texture/{}/{}/{}.png";
        std::string heightmap_cache_path = "assets/heightmap/{}/{}/{}.png";
        std::string normals_cache_path = "assets/normals/{}/{}/{}.png";

        /// Providers URLs template items
        /// URL always constructed from Zoom/X/Z and optional token
        /// Can be replaced with any provider following the XYZ (Slippy map) format
        /// and provide RGB heightmaps.
        std::string texture_host = RAYTILES_TEXTURE_HOST;
        std::string texture_url_path = RAYTILES_TEXTURE_URL_PATH;

        std::string heightmap_host = RAYTILES_HEIGHTMAP_HOST;
        std::string heightmap_url_path = RAYTILES_HEIGHTMAP_URL_PATH;

        std::string normals_host = RAYTILES_NORMALS_HOST;
        std::string normals_url_path = RAYTILES_NORMALS_URL_PATH;
    };

    // forward-declared so the public header doesn't drag httplib in via
    // downloader.hpp. defined in src/downloader.hpp.
    class pool;

    /// Per-frame driver that maintains the working set of tiles around a camera
    /// and renders them. One streamer manages one world; create more if you need
    /// independent worlds.
    ///
    /// Typical use:
    /// @code
    ///   raytiles::streamer s(conf, provider);
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
        /// @param conf            Tunable parameters; moved into the streamer.
        /// @param pool_conf       Tunable parameters for the tile downloader pool. moved into the pool.
        /// @note A raylib window must already be initialized (`InitWindow`) before
        ///       constructing a streamer because shader / texture creation requires
        ///       a live GL context.
        explicit streamer(const config &conf, const pool_config &pool_conf);

        ~streamer();

        streamer(const streamer &) = delete;

        streamer &operator=(const streamer &) = delete;

        streamer(streamer &&) noexcept;

        streamer &operator=(streamer &&) noexcept;

        /// Updates the desired tile set based on the camera and promotes any
        /// finished downloads into renderable GPU resources. Cheap to call every
        /// frame; internally rate-limited by `config::upload_budget_sec` and
        /// `config::max_uploads_per_frame`.
        void update(const Camera3D &camera);

        /// Renders all currently loaded tiles. Must be called between
        /// `BeginMode3D` / `EndMode3D` with the same camera passed to `update`.
        void draw(const Camera3D &camera);

        /// Draws a 2D HUD with streamer statistics (loaded / loading counts, etc).
        /// Call between `BeginDrawing` / `EndDrawing`, after `EndMode3D`.
        void debug(const Camera3D &camera) const;

        /// Draws 3D debug overlays (tile bounds, LOD seams). Call inside the same
        /// `BeginMode3D` / `EndMode3D` block as `draw`.
        void debug_3d(const Camera3D &camera) const;

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

        /// Set the fog start distance, the distance from the camera
        /// colors start to blend with fog
        void set_fog_start(float distance);

        /// Set the fog end distance, the distance from the camera
        /// colors are fully blended with fog color
        void set_fog_end(float distance);

        /// Set the heightmap scale factor, to increase or reduce
        /// the real height into desired (drama factor)
        void set_height_scale(float scale);

        /// Set the normals scale factor, to increase or reduce
        /// the lighting contrast.
        void set_normals_scale(float scale);

        /// Sets the sun direction vector for the displacement
        /// shader's lighting calculations.
        void set_sun_direction(Vector3 direction);

        /// Set the intensity of the sun lighting, to increase
        /// or reduce the contrast between lit and shaded areas.
        void set_sun_scale(float scale);

        /// Returns the terrain altitude (Y world-coordinate) under `position`,
        /// sampled from the heightmap pixel at the equivalent UV.
        /// @returns The altitude, or `nullopt` if no loaded tile covers the
        ///          queried XZ point. Callers should generally fall back to a
        ///          previous frame's value or to 0 when nullopt is returned.
        /// @note Each loaded tile keeps its decoded heightmap in CPU RAM (~192KB)
        ///       so this query is a direct pixel read; cost is O(1).
        [[nodiscard]] std::optional<float> ground_height(Vector3 position) const;

    private:
        void build_required(int zoom, int tx, int tz, float render_radius_sq);

        void process_loaded_tiles();

        void process_current_location();

        void remove_unused_tiles();

        void update_shader_uniforms();

        loading_tile spawn(const tile_key &tile);

        [[nodiscard]] bool is_tile_covered(const tile_key &key) const;

        [[nodiscard]] bool is_tile_out_of_area(const tile_key &key) const;

        config conf;
        raii::shader displacement_shader;
        // held by unique_ptr so the public header can forward-declare `pool`
        // and keep httplib out of every consumer's translation unit.
        std::unique_ptr<pool> tile_downloader;

        int rendered = 0;

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

        float ambient_light[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float fog_color[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        float sun_direction[3] = {0.1f, 1.0f, 0.1f};
        float fog_start = 1.0f;
        float fog_end = 100000.0f;
        float height_scale = 1.0f;
        float normals_scale = 1.0f;
        float sun_scale = 1.0f;

        raii::material material{};
        Vector3 last_position = {-9999.9f, -9999.9f, -9999.9f};

        std::unordered_set<tile_key> desired_keys;
        std::unordered_map<tile_key, loading_tile> loading_tiles;
        std::unordered_map<tile_key, loaded_tile> rendering_tiles;

        // metadata about tiles by their zoom
        std::unordered_map<int, tile_value> tiles;
    };
} // namespace raytiles

#endif  // RAYTILES_LIBRARY_H
