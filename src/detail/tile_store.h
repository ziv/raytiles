#pragma once
#include <array>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>

#include <vector>

#include "raylib.h"
#include "lod.hpp"
#include "tile.hpp"
#include "tile_source.h"
#include "utils.hpp"

namespace raytiles {
    struct tile_store_options {
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

        /// Per-zoom distance thresholds. Indexed as
        /// `thresholds[zoom - base_zoom]`. Only slots `[0, max_zoom - base_zoom]`
        /// are read.
        std::array<Meters, zoom_levels> thresholds = {
            100000.0f, 80000.0f, 40000.0f, 20000.0f, 10000.0f, 5000.0f, 2500.0f
        };

        /// Per-zoom skirt overlap factors. Indexed as
        /// `skirt_overlap[zoom - base_zoom]`. Only slots `[0, max_zoom - base_zoom]`
        /// are read.
        std::array<float, zoom_levels> skirt_overlap = {
            1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f
        };
    };

    class tile_store {
    public:
        tile_store(const tile_store_options &opts, tile_source_options source_opts);

        [[nodiscard]] std::optional<float> ground_height(const Vector3 &position) const;

        [[nodiscard]] bool is_loading() const;

        [[nodiscard]] float get_loading() const;

        /// Evicts resident tiles that are no longer needed (not desired and
        /// out of frustum / beyond the horizon / covered by other zooms).
        /// Call every frame, before `promote`.
        void reconcile(const Vector3 &position);

        /// Drains the source and uploads finished tiles to the GPU under the
        /// per-frame budget; keeps the render list sorted front-to-back.
        /// Call every frame, after `reconcile`.
        /// @param position     Camera position in absolute space — the origin
        ///                     the front-to-back sort measures from.
        /// @param world_offset Maps absolute tile coords to user space via
        ///                     `user = absolute + offset`; needed to bake the
        ///                     transform of tiles promoted this frame.
        void promote(const Vector3 &position, const Vector3 &world_offset);

        /// Rebuilds the desired set for `position` (pure lod policy), cancels
        /// loading tiles that fell out of it, and requests the missing ones.
        /// Call once at startup and whenever the camera moved far enough.
        void update_desired(const Vector3 &position);

        /// Frustum-tests every render item in place (and rebakes transforms
        /// after a large-world rebase). Call every frame, after `update_desired`.
        /// @param world_offset Maps absolute tile coords to user space (the
        ///                     `frustum`'s frame) via `user = absolute + offset`.
        void cull(const Frustum &frustum, const Vector3 &world_offset);

        /// The flat render list: everything the renderer needs, one entry per
        /// resident tile. Borrowed view — only valid within the current frame
        /// (promotion/eviction reallocate and reorder it).
        [[nodiscard]] std::span<const render_item> render_items() const { return render_list; }

    private:
        void evict(resident_tile &tile);

        /// Resolve the anchor and hand the tile to the source; tracked in
        /// `loading_keys` until a payload or drop comes back.
        void spawn(const tile_key &tile);

        [[nodiscard]] bool is_tile_out_of_area(const tile_key &key, const Vector3 &position) const;

        [[nodiscard]] bool is_tile_covered(const tile_key &key) const;

        /// Per-zoom metadata lookup; valid for zoom in [base_zoom, max_zoom].
        [[nodiscard]] const tile_value &zoom_value(Zoom zoom) const { return tiles[static_cast<std::size_t>(zoom - options.base_zoom)]; }

        tile_store_options options;
        bool loading = true;

        // desired-set policy inputs, derived once from `options`
        lod::options lod_opts;

        // scratch buffer reused by every desired-set rebuild so steady-state
        // recomputes allocate nothing
        std::vector<tile_key> desired_scratch;

        // set of desired keys required for current location
        // updates only when "update_desired" triggered
        std::unordered_set<tile_key> desired_keys;

        // keys handed to the source and not yet answered (payload or drop)
        std::unordered_set<tile_key> loading_keys;

        // payloads drained from the source but not yet uploaded — the budgeted
        // promote loop consumes from here; leftovers wait for the next frame
        std::vector<tile_payload> upload_queue;

        // drain scratch buffers (reused so steady-state drains allocate nothing)
        std::vector<tile_payload> ready_scratch;
        std::vector<tile_source::drop> dropped_scratch;

        // owner records of resident tiles (RAII resources + render-list slot)
        std::unordered_map<tile_key, resident_tile> resident_tiles;

        // flat render list: one draw-ready entry per resident tile, kept in
        // lockstep with `resident_tiles` (promote appends, evict swap-removes)
        std::vector<render_item> render_list;

        // world offset the render-list transforms were baked with; transforms
        // are rebaked only when this changes (large-world rebase)
        Vector3 baked_offset = {0.0f, 0.0f, 0.0f};

        // set when promotion/eviction disturbs the front-to-back order;
        // consumed at the end of promote (sort + slot rebuild)
        bool order_dirty = false;

        // coverage relations only change when tiles are promoted or the
        // desired set rebuilds; reconcile skips the (comparatively expensive)
        // is_tile_covered evictions while this is clear. exact, not a
        // heuristic: evictions only remove cover, which can only flip the
        // answer toward "keep".
        bool coverage_dirty = true;

        // metadata about tiles by their zoom, indexed zoom - base_zoom;
        // slots beyond max_zoom - base_zoom stay default-constructed and unused
        std::array<tile_value, zoom_levels> tiles;

        // background download workers
        tile_source source;
    };
}
