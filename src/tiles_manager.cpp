#include "raytiles/raytiles.h"
#include "detail/tiles_manager.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <format>
#include <ranges>
#include <unordered_map>
#include <utility>
#include <vector>
#include "detail/utils.hpp"

namespace raytiles {
    tiles_manager::tiles_manager(const tiles_manager_options &opts, tile_source_options source_opts)
        : options(opts),
          source(std::move(source_opts)) {
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

        // first time nothing is in flight or awaiting upload, loading is done
        if (loading && loading_keys.empty() && upload_queue.empty()) {
            loading = false;
        }
    }

    void tiles_manager::process_loaded_tiles(const Vector3 &world_offset) {
        // collect everything the source finished since last frame: one lock,
        // no polling. drops clear the loading bookkeeping (and log real
        // failures — cancellations are routine and stay quiet); payloads join
        // the upload queue.
        source.drain(ready_scratch, dropped_scratch);

        for (const auto &d: dropped_scratch) {
            loading_keys.erase(d.key);
            if (!d.cancelled) {
                // real failure: log it and let the next desired-set rebuild
                // retry — an immediate retry would hammer a failing server
                TraceLog(LOG_WARNING, "tile %d/%d/%d download failed: %s - dropping", d.key.zoom, d.key.x, d.key.z, d.reason.c_str());
            } else if (desired_keys.contains(d.key) && !rendering_tiles.contains(d.key)) {
                // cancelled, but wanted again by the time the drop arrived
                // (camera came back) — request it right away
                spawn(d.key);
            }
        }

        upload_queue.insert(upload_queue.end(), std::make_move_iterator(ready_scratch.begin()), std::make_move_iterator(ready_scratch.end()));
        ready_scratch.clear();

        // budgeted GPU upload: bounded by a wall-clock budget (keeps the frame
        // steady on slow GPUs) and a hard cap (keeps heavy single-tile uploads
        // from running away). payloads that don't fit wait in the queue.
        const double frame_start = GetTime();
        int promoted = 0;

        while (!upload_queue.empty()) {
            tile_payload payload = std::move(upload_queue.back());
            upload_queue.pop_back();
            const tile_key key = payload.key;
            loading_keys.erase(key);

            // do we still need it? (raii frees the images if not)
            if (!desired_keys.contains(key)) continue;

            // upload to GPU. the heightmap CPU image is kept in the loaded_tile
            // for ground_height() queries (recast, collision).
            raii::texture texture_tex = raii::load_texture_from_image(*payload.albedo);
            raii::texture height_tex = raii::load_texture_from_image(*payload.height);
            raii::texture normals_tex = raii::load_texture_from_image(*payload.normals);
            raii::image height_img = std::move(payload.height);

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
            const double abs_x = (static_cast<double>(key.x) + 0.5) * static_cast<double>(tv.size);
            const double abs_z = (static_cast<double>(key.z) + 0.5) * static_cast<double>(tv.size);
            const auto user_x = static_cast<float>(abs_x + static_cast<double>(world_offset.x));
            const auto user_z = static_cast<float>(abs_z + static_cast<double>(world_offset.z));
            const render_item item{
                *tv.mesh,
                *texture_tex,
                *height_tex,
                *normals_tex,
                MatrixTranslate(user_x, 0.0f, user_z),
                tv.size,
                abs_x,
                abs_z,
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

        // cancel once, here: the desired set only changes in this function, so
        // this is the single place a loading tile can fall out of it
        for (const auto &key: loading_keys)
            if (!desired_keys.contains(key)) source.cancel(key);

        // spawn new if not in rendering list
        for (const auto &key: desired_keys)
            if (!rendering_tiles.contains(key) && !loading_keys.contains(key))
                spawn(key);
    }

    void tiles_manager::spawn(const tile_key &tile) {
        const auto scale = 1 << (tile.zoom - options.base_zoom);
        source.request(tile_request{
            tile,
            tile.x + options.anchor_x_tile * scale,
            tile.z + options.anchor_z_tile * scale,
        });
        loading_keys.insert(tile);
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
