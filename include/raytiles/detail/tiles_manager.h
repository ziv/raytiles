#pragma once
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "raylib.h"
#include "downloader.h"
#include "tile.hpp"
#include "utils.hpp"

namespace raytiles {
    struct tiles_manager_options {
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

        /// World-space anchor in tile coordinates at `base_zoom`. The streamer
        /// translates tile XY to world XZ relative to this anchor so the world
        /// origin is wherever you want it (e.g. your runway).
        int anchor_x_tile = 306;
        int anchor_z_tile = 207;

        /// Radius, in `world_config::base_zoom` tiles, of the disc of tiles
        /// loaded around the camera. Larger values = more tiles in flight =
        /// more memory / bandwidth.
        int rendering_radius = 6;

        /// Near clip plane (meters) used by the displacement shader for fog and
        /// depth-precision tuning. Match this to your camera setup.
        MetersD near_plane = 1;

        /// Far clip plane (meters) used by the displacement shader for fog and
        /// depth-precision tuning. Match this to your camera setup.
        MetersD far_plane = 400000;

        /// Generate trilinear / anisotropic mipmaps for the albedo texture on
        /// upload. Strongly recommended; avoids shimmering at distance.
        bool use_mipmap = true;

        /// Wall-clock budget (in seconds) per frame for promoting downloaded tiles
        /// into GPU resources. Caps the cost of a single bursty frame.
        double upload_budget_sec = 0.002;

        /// Hard cap on tile promotions per frame, on top of `upload_budget_sec`.
        /// Whichever limit is hit first stops the loop.
        int max_uploads_per_frame = 8;

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

        /// Per-zoom skirt overlap factors, allowing you to tweak the amount of overlap
        /// (and thus fill rate) at different zoom levels. Baked into generated meshes.
        std::unordered_map<Zoom, float> skirt_overlap = {
            {9, 1.00f},
            {10, 1.00f},
            {11, 1.00f},
            {12, 1.00f},
            {13, 1.00f},
            {14, 1.00f},
            {15, 1.00f}
        };
    };

    class tiles_manager {
    public:
        tiles_manager(tiles_manager_options opts, pool_config pool_conf);

        [[nodiscard]] std::optional<float> ground_height(const Vector3 &position) const;

        [[nodiscard]] bool is_loading() const;

        [[nodiscard]] std::size_t loading_count() const;

        [[nodiscard]] float get_loading() const;

        //// Pre-processing tiles.
        /// Should be called every frame and before "process".
        void pre_process(const Vector3 &position);

        /// Process tiles for current location.
        /// Must be called once, and then after position changed.
        void process(const Vector3 &position);

        /// Post-process tiles.
        /// Should be called evry frame and after "process".
        void post_process(const Frustum &frustum);

        /// Bundles internal state into a `DebugView` for the renderer's draw /
        /// debug paths. The returned view borrows references to this manager's
        /// internal maps; do not retain it beyond the current frame.
        [[nodiscard]] DebugView make_debug_view(Frustum &frustum);

    private:
        void process_loaded_tiles();

        void process_current_location(const Vector3 &position);

        void build_required(const Vector3 &position, Zoom zoom, int tx, int tz, float render_radius_sq);

        [[nodiscard]] loading_tile spawn(const tile_key &tile);

        [[nodiscard]] bool is_tile_out_of_area(const tile_key &key, const Vector3 &position) const;

        [[nodiscard]] bool is_tile_covered(const tile_key &key) const;

        tiles_manager_options options;
        bool loading = true;
        // Vector3 last_position = {-9999.9f, -9999.9f, -9999.9f};

        // set of desired keys required for current location
        // updates only when "process_current_location" triggered
        std::unordered_set<tile_key> desired_keys;

        // map of current loading tiles and their futures
        std::unordered_map<tile_key, loading_tile> loading_tiles;

        // map of tiles that may be rendered if in frustum
        // contain reference to the GPU nad CPU loaded resources
        std::unordered_map<tile_key, loaded_tile> rendering_tiles;

        // metadata about tiles by their zoom
        std::unordered_map<Zoom, tile_value> tiles;

        // background download workers
        pool tile_downloader;
    };
}
