/// @file raytiles.h
/// Public API for the raytiles library: stream a 3D world built from satellite
/// imagery and heightmap tiles around a moving camera and render it via raylib.
#ifndef RAYTILES_LIBRARY_H
#define RAYTILES_LIBRARY_H
#include <memory>
#include <optional>
#include <string>
#include "raylib.h"

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
#define RAYTILES_NORMALS_URL_PATH "/elevation-tiles-prod/normals/{zoom}/{x}/{y}.png"
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
        int base_zoom = 11;

        /// Highest level-of-detail zoom available. Tiles directly under the camera
        /// are subdivided up to this zoom.
        int max_zoom = 14;

        /// World size (in meters) of one tile at `base_zoom`. Tiles at higher zooms
        /// are scaled by `1 / (1 << (zoom - base_zoom))`.
        float base_zoom_tile_size = 16600.0f;

        /// Radius, in `base_zoom` tiles, of the disc of tiles loaded around the
        /// camera. Larger values = more tiles in flight = more memory / bandwidth.
        int rendering_radius = 7;

        /// Skirt geometry overlap factor (per side) used to hide cracks between
        /// neighboring tiles at different LODs.
        float skirt_size = 15.0f;

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
        int anchor_x_tile = 1223;
        int anchor_z_tile = 828;

        /// Near / far clip planes used by the displacement shader for fog and
        /// depth precision tuning. Match these to your camera setup.
        double near_plane = 1;
        double far_plane = 100000;

        /// Generate trilinear / anisotropic mipmaps for the albedo texture on
        /// upload. Strongly recommended; avoids shimmering at distance.
        bool use_mipmap = true;

        /// Vertical drop (in meters) of the skirt geometry below each tile's edge.
        /// Larger values hide cracks more reliably but cost more fill rate.
        float skirt_drop = 1000.0f;

        /// Distance (in meters) at which atmospheric fog starts to fade tiles to
        /// `set_fog_color`'s color.
        float fog_start = 40000.0f;

        /// Distance (in meters) at which fog reaches full cover.
        float fog_end = 70000.0f;

        /// Weather to log from the threads or from the main process
        /// Logging from main thread/process is done via raylib's TraceLog function
        bool use_logger = false;
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

    class manager;

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
        void update(const Camera3D &camera) const;

        /// Renders all currently loaded tiles. Must be called between
        /// `BeginMode3D` / `EndMode3D` with the same camera passed to `update`.
        void draw(const Camera3D &camera) const;

        /// Draws a 2D HUD with streamer statistics (loaded / loading counts, etc).
        /// Call between `BeginDrawing` / `EndDrawing`, after `EndMode3D`.
        void debug(const Camera3D &camera) const;

        /// Draws 3D debug overlays (tile bounds, LOD seams). Call inside the same
        /// `BeginMode3D` / `EndMode3D` block as `draw`.
        void debug_3d(const Camera3D &camera) const;

        /// Sets the ambient light color sent to the displacement shader. Use this
        /// to drive day / night / weather lighting changes.
        void set_ambient_light(Color color) const;

        /// Sets the fog color for distance attenuation. Match this to your sky
        /// color for a seamless horizon.
        void set_fog_color(Color color) const;

        /// Set the fog start distance, the distance from the camera
        /// colors start to blend with fog
        void set_fog_start(float distance) const;

        /// Set the fog end distance, the distance from the camera
        /// colors are fully blended with fog color
        void set_fog_end(float distance) const;

        /// Set the heightmap scale factor, to increase or reduce
        /// the real height into desired (drama factor)
        void set_height_scale(float scale) const;

        /// Set the normals scale factor, to increase or reduce
        /// the lighting contrast.
        void set_normals_scale(float scale) const;

        /// Sets the sun direction vector for the displacement
        /// shader's lighting calculations.
        void set_sun_direction(Vector3 direction) const;

        /// Set the intensity of the sun lighting, to increase
        /// or reduce the contrast between lit and shaded areas.
        void set_sun_scale(float scale) const;

        /// Returns the terrain altitude (Y world-coordinate) under `position`,
        /// sampled from the heightmap pixel at the equivalent UV.
        /// @returns The altitude, or `nullopt` if no loaded tile covers the
        ///          queried XZ point. Callers should generally fall back to a
        ///          previous frame's value or to 0 when nullopt is returned.
        /// @note Each loaded tile keeps its decoded heightmap in CPU RAM (~192KB)
        ///       so this query is a direct pixel read; cost is O(1).
        [[nodiscard]] std::optional<float> ground_height(Vector3 position) const;

    private:
        std::unique_ptr<manager> impl;
    };
} // namespace raytiles

#endif  // RAYTILES_LIBRARY_H
