#include "raytiles/raytiles.h"
#include "detail/tiles_manager.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <format>
#include <ranges>
#include <unordered_map>
#include <utility>
#include <vector>
#include "detail/utils.hpp"

using namespace std::chrono_literals;

namespace raytiles {
    tiles_manager::tiles_manager(const tiles_manager_options &opts, pool_options pool_opts)
        : options(opts),
          tile_downloader(std::move(pool_opts)) {
        // input validation
        if (options.base_zoom < min_supported_zoom) {
            throw std::runtime_error(std::format("base_zoom {} is below min_supported_zoom {}", options.base_zoom, min_supported_zoom));
        }
        if (options.max_zoom > max_supported_zoom) {
            throw std::runtime_error(std::format("max_zoom {} is above max_supported_zoom {}", options.max_zoom, max_supported_zoom));
        }
        if (options.max_zoom < options.base_zoom) {
            throw std::runtime_error(std::format("max_zoom {} is below base_zoom {}", options.max_zoom, options.base_zoom));
        }

        // desired-set policy inputs (pure lod module)
        lod_opts = lod::options{
            options.base_zoom,
            options.max_zoom,
            options.base_zoom_tile_size,
            options.rendering_radius,
            options.thresholds,
        };

        // construct the tiles map
        // for each zoom level:
        // - metadata (size & threshold)
        // - mesh
        int res = min_resolution;
        for (int zoom = options.base_zoom; zoom <= options.max_zoom; ++zoom) {
            const auto idx = static_cast<std::size_t>(zoom - options.base_zoom);
            const auto ratio = static_cast<float>(1 << (zoom - options.base_zoom));
            const auto size = options.base_zoom_tile_size / ratio;
            const auto th = options.thresholds[idx];
            const auto skirt_factor = options.skirt_overlap[idx];

            tiles[idx] = tile_value{
                size,
                th * th,
                raii::mesh{GenMeshPlane(size * skirt_factor, size * skirt_factor, res, res)}
            };
            res = std::min(res * 2, max_resolution);
        }
    }

    bool tiles_manager::is_loading() const {
        return loading;
    }

    std::size_t tiles_manager::loading_count() const {
        return loading_tiles.size();
    }

    float tiles_manager::get_loading() const {
        // fraction of the desired set that is resident. monotonic during the
        // initial load, 1.0 when everything desired is on the GPU.
        if (desired_keys.empty()) return 0.0f;
        std::size_t resident = 0;
        for (const auto &key: desired_keys)
            if (rendering_tiles.contains(key)) ++resident;
        return static_cast<float>(resident) / static_cast<float>(desired_keys.size());
    }

    std::optional<float> tiles_manager::ground_height(const Vector3 &position) const {
        // walk from the highest available zoom down to base; whichever zoom holds the
        // tile that contains (position.x, position.z) wins. higher zoom = finer
        // sample, so we prefer it if loaded.
        for (int zoom = options.max_zoom; zoom >= options.base_zoom; --zoom) {
            const float size = zoom_value(zoom).size;

            const int tile_x = static_cast<int>(std::floor(position.x / size));
            const int tile_z = static_cast<int>(std::floor(position.z / size));

            const auto it = rendering_tiles.find(tile_key{zoom, tile_x, tile_z});
            if (it == rendering_tiles.end()) continue;

            const auto &tile = it->second;
            const Image &img = *tile.hm_image;

            // defensive: promotion only stores decoded images, but a garbage
            // read here would be silent — keep the guard
            if (!IsImageValid(img)) continue;

            // local uv inside the tile, [0, 1)
            const float u = (position.x - static_cast<float>(tile_x) * size) / size;
            const float v = (position.z - static_cast<float>(tile_z) * size) / size;

            const int px = static_cast<int>(u * static_cast<float>(img.width));
            const int py = static_cast<int>(v * static_cast<float>(img.height));

            return utils::get_height_from_image(img, px, py);
        }
        return std::nullopt;
    }


    void tiles_manager::pre_process(const Vector3 &position, const Vector3 &world_offset) {
        // gc
        std::erase_if(rendering_tiles, [&](auto &entry) {
            auto &[key, tile] = entry;
            const bool remove = [&] {
                // if it in desired, keep it
                if (desired_keys.contains(key)) return false;

                // if it is base zoom and not desired, no need to
                // check the rest, remove it. it the horizon.
                if (key.zoom == options.base_zoom) return true;

                // if not in desired and not in frustum, remove
                // without thinking todo add softer eviction
                if (!render_list[tile.slot].visible) return true;

                // if the tile is far beyond the horizon, remove
                // without thinking
                if (is_tile_out_of_area(key, position)) return true;

                // here we stop to think (this is the "slow" path)
                // if the tile is not covered by other tiles, keep
                // it to avoid holes in the surface
                if (!is_tile_covered(key)) return false;
                return true;
            }();
            if (remove) evict(tile);
            return remove;
        });

        for (auto &key: loading_tiles | std::views::keys) {
            if (!desired_keys.contains(key)) {
                tile_downloader.cancel(key.zoom, key.x, key.z);
            }
        }

        process_loaded_tiles(world_offset);

        // keep the list front-to-back for early-Z. only membership changes
        // (promote/evict) disturb the order, so steady-state frames skip this;
        // opaque rendering means order is a perf policy, never a visual one.
        if (order_dirty) {
            order_dirty = false;
            const auto dist_sq = [&](const render_item &item) {
                const double dx = item.abs_x - static_cast<double>(position.x);
                const double dz = item.abs_z - static_cast<double>(position.z);
                return dx * dx + dz * dz;
            };
            std::ranges::sort(render_list, [&](const render_item &a, const render_item &b) { return dist_sq(a) < dist_sq(b); });
            for (std::uint32_t i = 0; i < render_list.size(); ++i) rendering_tiles.at(render_list[i].key).slot = i;
        }

#ifndef NDEBUG
        // render-list/owner lockstep invariant (swap-remove + sort bookkeeping)
        for (std::uint32_t i = 0; i < render_list.size(); ++i) assert(rendering_tiles.at(render_list[i].key).slot == i);
#endif
    }

    void tiles_manager::evict(loaded_tile &tile) {
        // swap-with-last removal from the flat list; the moved item's owner
        // record is re-pointed via the item's key backlink
        order_dirty = true;
        const auto slot = tile.slot;
        const auto last = static_cast<std::uint32_t>(render_list.size() - 1);
        if (slot != last) {
            render_list[slot] = render_list[last];
            rendering_tiles.at(render_list[slot].key).slot = slot;
        }
        render_list.pop_back();
    }

    void tiles_manager::process(const Vector3 &position) {
        process_current_location(position);
    }

    void tiles_manager::post_process(const Frustum &frustum, const Vector3 &world_offset) {
        // rebake transforms only after a large-world rebase. exact compare is
        // correct: the caller passes the same bits every frame until it rebases.
        if (world_offset.x != baked_offset.x || world_offset.z != baked_offset.z) {
            baked_offset = world_offset;
            const auto off_x = static_cast<double>(world_offset.x);
            const auto off_z = static_cast<double>(world_offset.z);
            for (auto &item: render_list) {
                // keep the addition in double so the huge-tile-coord + huge-offset
                // cancellation happens at full precision before the float cast
                item.transform = MatrixTranslate(static_cast<float>(item.abs_x + off_x), 0.0f, static_cast<float>(item.abs_z + off_z));
            }
        }

        for (auto &item: render_list) {
            item.visible = utils::is_tile_in_frustum(item.transform.m12, item.transform.m14, item.size, frustum);
        }

        // first time the loading list is empty, means we finished loading
        if (loading && loading_tiles.empty()) {
            loading = false;
        }
    }

    void tiles_manager::process_loaded_tiles(const Vector3 &world_offset) {
        // walk loading tiles; for each entry where all three downloads finished, either
        // promote it to rendering_tiles or drop it (no longer desired). entries are
        // erased immediately on the iterator. uploads are bounded both by a wall-clock
        // budget (to keep the frame steady on slow GPUs) and a hard cap (to keep heavy
        // single-tile uploads from running away). PNG decode happened off-thread inside
        // the worker pool, so this loop only does GPU upload + bookkeeping.
        const double frame_start = GetTime();
        int promoted = 0;

        for (auto it = loading_tiles.begin(); it != loading_tiles.end();) {
            auto &[key, tile] = *it;

            // all three futures must be ready
            if (tile.tx_future.wait_for(0s) != std::future_status::ready ||
                tile.hm_future.wait_for(0s) != std::future_status::ready ||
                tile.nl_future.wait_for(0s) != std::future_status::ready) {
                ++it;
                continue;
            }

            // futures resolved with already-decoded raylib Image values (POD).
            // .get() returns a const Image& into the shared_future's storage; the
            // pixel buffer was malloc'd by stb_image in the worker. we copy the
            // struct out (cheap, just pointer + ints) and immediately wrap each
            // copy in raii::image so the buffer is freed by UnloadImage on every
            // exit path below. the shared_future's residual copy of the Image is
            // harmless when the loading_tile is erased: Image is a POD with no
            // destructor, so destroying the shared_future does not double-free.
            //
            // a worker exception (network failure, decode failure, etc.) propagates
            // through .get(); drop the tile and keep streaming the rest.
            raii::image tex_img{};
            raii::image height_img{};
            raii::image normals_img{};
            try {
                tex_img = raii::image{tile.tx_future.get()};
                height_img = raii::image{tile.hm_future.get()};
                normals_img = raii::image{tile.nl_future.get()};
            } catch (const std::exception &e) {
                TraceLog(LOG_WARNING, "tile %d/%d/%d download failed: %s - dropping", key.zoom, key.x, key.z, e.what());
                it = loading_tiles.erase(it);
                continue;
            }

            // do we still need it?
            if (!desired_keys.contains(key)) {
                it = loading_tiles.erase(it);
                continue;
            }

            // upload to GPU and move into rendering_tiles. the heightmap CPU image is
            // kept in the loaded_tile for ground_height() queries (recast, collision).
            raii::texture texture_tex = raii::load_texture_from_image(*tex_img);
            raii::texture height_tex = raii::load_texture_from_image(*height_img);
            raii::texture normals_tex = raii::load_texture_from_image(*normals_img);

            // don't clamp the ends
            SetTextureWrap(*texture_tex, TEXTURE_WRAP_CLAMP);
            SetTextureWrap(*height_tex, TEXTURE_WRAP_CLAMP);
            SetTextureWrap(*normals_tex, TEXTURE_WRAP_CLAMP);

            // allow use of mipmaps (only for texture)
            if (options.use_mipmap) {
                GenTextureMipmaps(&texture_tex.get());
                SetTextureFilter(*texture_tex, TEXTURE_FILTER_ANISOTROPIC_16X);
            }

            // draw-ready entry for the flat render list. transform is baked in
            // user space; the double add preserves precision before the float cast.
            const auto &tv = zoom_value(key.zoom);
            const auto user_x = static_cast<float>(tile.tx + static_cast<double>(world_offset.x));
            const auto user_z = static_cast<float>(tile.tz + static_cast<double>(world_offset.z));
            const render_item item{
                *tv.mesh,
                *texture_tex,
                *height_tex,
                *normals_tex,
                MatrixTranslate(user_x, 0.0f, user_z),
                tv.size,
                tile.tx,
                tile.tz,
                key,
                false, // visible: decided by post_process later this frame
                true, // desired: promotion only happens for still-desired keys
            };

            if (const auto existing = rendering_tiles.find(key); existing != rendering_tiles.end()) {
                // defensive: key already resident (shouldn't happen via the
                // spawn/promote flow) — replace resources in place, keep the slot
                render_list[existing->second.slot] = item;
                existing->second = loaded_tile{std::move(texture_tex), std::move(height_tex), std::move(height_img), std::move(normals_tex),
                                               existing->second.slot};
            } else {
                render_list.push_back(item);
                const auto slot = static_cast<std::uint32_t>(render_list.size() - 1);
                rendering_tiles.emplace(key, loaded_tile{std::move(texture_tex), std::move(height_tex), std::move(height_img), std::move(normals_tex), slot});
                order_dirty = true;
            }

            it = loading_tiles.erase(it);
            ++promoted;

            if (promoted >= options.max_uploads_per_frame) break;
            if (GetTime() - frame_start >= options.upload_budget_sec) break;
        }
    }

    void tiles_manager::process_current_location(const Vector3 &position) {
        // the policy is pure; membership indexing stays here
        desired_scratch.clear();
        lod::desired_tiles(lod_opts, position, desired_scratch);

        desired_keys.clear();
        desired_keys.insert(desired_scratch.begin(), desired_scratch.end());

        // refresh the debug-overlay flag on resident items (rebuilds are rare)
        for (auto &item: render_list) item.desired = desired_keys.contains(item.key);

        // spawn new if not in rendering list
        for (const auto &key: desired_keys)
            if (!rendering_tiles.contains(key) && !loading_tiles.contains(key))
                loading_tiles.try_emplace(key, spawn(key));
    }

    loading_tile tiles_manager::spawn(const tile_key &tile) {
        const auto &te = zoom_value(tile.zoom);
        const auto scale = 1 << (tile.zoom - options.base_zoom);
        const auto tx = tile.x + options.anchor_x_tile * scale;
        const auto tz = tile.z + options.anchor_z_tile * scale;
        const auto tile_size = te.size;

        auto t = loading_tile{
            // loading tile structure
            (static_cast<double>(tile.x) + 0.5) * static_cast<double>(tile_size),
            (static_cast<double>(tile.z) + 0.5) * static_cast<double>(tile_size),
            tile_downloader.enqueue_texture(tile.zoom, tx, tz),
            tile_downloader.enqueue_heightmap(tile.zoom, tx, tz),
            tile_downloader.enqueue_normals(tile.zoom, tx, tz),
        };

        return t;
    }

    bool tiles_manager::is_tile_out_of_area(const tile_key &key, const Vector3 &position) const {
        const MetersDSq distance_sq = utils::distance_sq_to_tile_xz(position, key, zoom_value(key.zoom).size);
        return distance_sq > utils::calculate_horizon(position);
    }

    bool tiles_manager::is_tile_covered(const tile_key &key) const {
        const auto contains = [&](const int zoom, const int x, const int z) { return rendering_tiles.contains(tile_key{zoom, x, z}); };

        // check parent
        if (key.zoom > options.base_zoom) {
            if (contains(key.zoom - 1, key.x >> 1, key.z >> 1)) return true;
        }

        // check children
        if (key.zoom < options.max_zoom) {
            const int child_x = key.x * 2;
            const int child_z = key.z * 2;
            if (const int target_zoom = key.zoom + 1; contains(target_zoom, child_x, child_z) &&
                                                      contains(target_zoom, child_x + 1, child_z) &&
                                                      contains(target_zoom, child_x, child_z + 1) &&
                                                      contains(target_zoom, child_x + 1, child_z + 1)) {
                return true;
            }
        }

        // check grandparent. rare, but happens when zoom levels are skipped
        // due to distance-based loading in a very fast movement.
        if (key.zoom - 1 > options.base_zoom) {
            if (contains(key.zoom - 2, key.x >> 2, key.z >> 2)) return true;
        }

        // check grandchildren. rare, the same reason.
        if (key.zoom + 1 < options.max_zoom) {
            const int child_x = key.x * 4;
            const int child_z = key.z * 4;
            const int target_zoom = key.zoom + 2;
            for (int ox = 0; ox < 4; ++ox)
                for (int oz = 0; oz < 4; ++oz)
                    if (!contains(target_zoom, child_x + ox, child_z + oz))
                        return false;
            return true;
        }

        return false;
    }
}
