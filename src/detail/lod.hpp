#pragma once
/// @file lod.hpp
/// Pure level-of-detail policy: which tiles should be resident for a given
/// camera position.
///
/// This module is deliberately free of raylib calls, GL state, I/O, and
/// mutable globals — `desired_tiles` is a deterministic function of
/// (options, position). That keeps the subtle quadtree/horizon math unit
/// testable without a window (see tests/lod_tests.cpp).
///
/// The algorithm, top-down:
///  1. Walk the disc of base-zoom tiles of `radius` around the camera
///     (strictly inside `(radius - 1)^2`, matching the historical behavior).
///  2. For each tile, recursively decide between three outcomes:
///       - beyond the horizon distance -> not desired at all,
///       - farther than this zoom's threshold -> desired at this zoom (stop),
///       - nearer than the threshold -> subdivide into the 4 child tiles at
///         zoom+1 and recurse (the parent itself is NOT desired).
///  3. At `max_zoom` recursion always stops and the tile is desired.
///
/// Consequences worth knowing:
///  - The output never contains an ancestor and its descendant at once.
///  - Distance is measured to the tile *center*, in 3D (camera height is
///    included) so a high camera keeps coarser zooms directly below it.
///  - The horizon cutoff scales with sqrt(height): tiles a ground-level
///    camera cannot possibly see are never requested.

#include <array>
#include <unordered_set>

#include "raytiles/raytiles.h" // Zoom / Meters aliases, zoom_levels, supported-zoom bounds
#include "tile.hpp"
#include "utils.hpp"

namespace raytiles::lod {
    /// Inputs of the LOD decision. A subset of the public `world_config` /
    /// `streaming_config` — kept minimal so tests can construct it directly.
    struct options {
        /// Lowest zoom in play; the disc walk happens at this zoom.
        int base_zoom = min_supported_zoom;

        /// Highest zoom; recursion stops (and always desires) here.
        int max_zoom = max_supported_zoom;

        /// World size (meters) of one tile at `base_zoom`. Tiles at zoom z
        /// measure `base_tile_size / 2^(z - base_zoom)`.
        float base_tile_size = 66400.0f;

        /// Disc radius around the camera, in base-zoom tiles. The walk covers
        /// offsets with `dx^2 + dz^2 < (radius - 1)^2`.
        int radius = 6;

        /// Per-zoom subdivision distances in meters, indexed
        /// `[zoom - base_zoom]`. A tile nearer than its zoom's threshold is
        /// split into its 4 children instead of being desired itself.
        std::array<Meters, zoom_levels> thresholds = {
            100000.0f, 80000.0f, 40000.0f, 20000.0f, 10000.0f, 5000.0f, 2500.0f
        };
    };

    namespace detail {
        /// Per-zoom values derived from `options` once per call: tile size and
        /// squared threshold, both indexed `[zoom - base_zoom]`.
        struct derived {
            // zero-init: slots beyond `max_zoom - base_zoom` stay unused
            std::array<float, zoom_levels> sizes{};
            std::array<MetersDSq, zoom_levels> thresholds_sq{};

            explicit derived(const options &o) {
                for (int zoom = o.base_zoom; zoom <= o.max_zoom; ++zoom) {
                    const auto idx = static_cast<std::size_t>(zoom - o.base_zoom);
                    const auto ratio = static_cast<float>(1 << (zoom - o.base_zoom));
                    sizes[idx] = o.base_tile_size / ratio;
                    thresholds_sq[idx] = static_cast<MetersDSq>(o.thresholds[idx]) * static_cast<MetersDSq>(o.thresholds[idx]);
                }
            }
        };

        /// Recursive subdivision step (outcome 2 of the file comment).
        /// `horizon_sq` is the squared view-distance limit for this camera.
        inline void build_required(const options &o, const derived &d, const Vector3 &position,
                                   const Zoom zoom, const int tx, const int tz,
                                   const MetersDSq horizon_sq, std::unordered_set<tile_key> &out) {
            if (zoom == o.max_zoom) {
                out.insert({zoom, tx, tz});
                return;
            }

            const auto idx = static_cast<std::size_t>(zoom - o.base_zoom);
            const MetersDSq distance_sq = utils::distance_sq_to_tile(position, {zoom, tx, tz}, d.sizes[idx]);

            // not in the area we render at all
            if (distance_sq > horizon_sq) return;

            // far enough to be rendered at this zoom; recursion stops
            if (distance_sq >= d.thresholds_sq[idx]) {
                out.insert({zoom, tx, tz});
                return;
            }

            // too close: this tile must be shown in finer detail
            const int child_zoom = zoom + 1;
            const int cx0 = tx * 2;
            const int cz0 = tz * 2;
            for (int ox = 0; ox < 2; ++ox)
                for (int oz = 0; oz < 2; ++oz)
                    build_required(o, d, position, child_zoom, cx0 + ox, cz0 + oz, horizon_sq, out);
        }
    } // namespace detail

    /// Computes the set of tiles that should be resident for a camera at
    /// `position` (absolute world space, meters). Clears and refills `out`
    /// (the caller keeps the set to reuse its allocation across updates).
    inline void desired_tiles(const options &o, const Vector3 &position, std::unordered_set<tile_key> &out) {
        out.clear();
        const detail::derived d(o);

        const int current_tile_x = static_cast<int>(std::floor(position.x / o.base_tile_size));
        const int current_tile_z = static_cast<int>(std::floor(position.z / o.base_tile_size));

        const auto r = o.radius;
        const auto allowed_radius = (r - 1) * (r - 1);

        // rendering limit based on the horizon distance at the camera height:
        // tiles beyond that point cannot be visible, so don't even request them.
        const auto horizon_sq = static_cast<MetersDSq>(utils::calculate_horizon(position));

        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz)
                if (dz * dz + dx * dx < allowed_radius)
                    detail::build_required(o, d, position, o.base_zoom,
                                           current_tile_x + dx, current_tile_z + dz, horizon_sq, out);
    }
} // namespace raytiles::lod
