#include "raytiles/raytiles.h"
#include "detail/tiles_manager.h"
#include <format>
#include <ranges>
#include <utility>
#include <chrono>
#include "detail/utils.hpp"

using namespace std::chrono_literals;

namespace raytiles {
    tiles_manager::tiles_manager(tiles_manager_options opts, pool_options pool_opts)
        : options(std::move(opts)),
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

            tiles[zoom] = tile_value{
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
        if (loading_tiles.empty()) return 0.0f;
        const auto required = static_cast<float>(desired_keys.size());
        if (required == 0.0f) return 0.0f; // avoid division by zero, should not happen but just in case
        const auto loaded = static_cast<float>(loading_tiles.size());
        return 1 - loaded / required;
    }

    std::optional<float> tiles_manager::ground_height(const Vector3 &position) const {
        // walk from the highest available zoom down to base; whichever zoom holds the
        // tile that contains (position.x, position.z) wins. higher zoom = finer
        // sample, so we prefer it if loaded.
        for (int zoom = options.max_zoom; zoom >= options.base_zoom; --zoom) {
            const auto &t = tiles.at(zoom);
            // const float size = tile_sizes[zoom - conf.base_zoom];
            const float size = t.size;

            const int tile_x = static_cast<int>(std::floor(position.x / size));
            const int tile_z = static_cast<int>(std::floor(position.z / size));

            const auto it = rendering_tiles.find(tile_key{zoom, tile_x, tile_z});
            if (it == rendering_tiles.end()) continue;

            const auto &tile = it->second;
            const Image &img = *tile.hm_image;

            // todo never suppose to happen, renderer holds only valid images. remove?
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


    void tiles_manager::pre_process(const Vector3 &position) {
        // gc
        std::erase_if(rendering_tiles, [&](const auto &item) {
            // if it in desired, keep it
            if (desired_keys.contains(item.first)) return false;

            // if it is base zoom and not desired, no need to
            // check the rest, remove it. it the horizon.
            if (item.first.zoom == options.base_zoom) return true;

            // if not in desired and not in frustum, remove
            // without thinking todo add softer eviction
            if (!item.second.in_frustum_this_frame) return true;

            // if the tile is far beyond the horizon, remove
            // without thinking
            if (is_tile_out_of_area(item.first, position)) return true;

            // here we stop to think (this is the "slow" path)
            // if the tile is not covered by other tiles, keep
            // it to avoid holes in the surface
            if (!is_tile_covered(item.first)) return false;
            return true;
        });

        for (auto &key: loading_tiles | std::views::keys) {
            if (!desired_keys.contains(key)) {
                tile_downloader.cancel(key.zoom, key.x, key.z);
            }
        }

        process_loaded_tiles();
    }

    void tiles_manager::process(const Vector3 &position) {
        process_current_location(position);
    }

    void tiles_manager::post_process(const Frustum &frustum, const Vector3 &world_offset) {
        for (auto &tile: rendering_tiles | std::views::values) {
            // Shift absolute tile center into user space before testing
            // against the user-space frustum.
            const float user_x = static_cast<float>(tile.tx + static_cast<double>(world_offset.x));
            const float user_z = static_cast<float>(tile.tz + static_cast<double>(world_offset.z));
            tile.in_frustum_this_frame = utils::is_tile_in_frustum(user_x, user_z, tile.size, frustum);
        }
        // first time the loading list is empty, means we finished loading
        if (loading && loading_tiles.empty()) {
            loading = false;
        }
    }

    DataView tiles_manager::make_debug_view(Frustum &frustum) {
        return DataView{frustum, rendering_tiles, tiles, desired_keys};
    }

    void tiles_manager::process_loaded_tiles() {
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

            rendering_tiles.insert_or_assign(key, loaded_tile{
                                                 tiles.at(key.zoom).size,
                                                 tile.tx,
                                                 tile.tz,
                                                 std::move(texture_tex),
                                                 std::move(height_tex),
                                                 std::move(height_img),
                                                 std::move(normals_tex),
                                                 false
                                             });

            it = loading_tiles.erase(it);
            ++promoted;

            if (promoted >= options.max_uploads_per_frame) break;
            if (GetTime() - frame_start >= options.upload_budget_sec) break;
        }
    }

    void tiles_manager::process_current_location(const Vector3 &position) {
        desired_keys.clear();
        const int current_tile_x = static_cast<int>(std::floor(position.x / options.base_zoom_tile_size));
        const int current_tile_z = static_cast<int>(std::floor(position.z / options.base_zoom_tile_size));

        // scanning radius: 10 -> is ((10 * 2 + 1) * 33km) width -> ~ 700km -> max horizon distance * 2
        const auto r = options.rendering_radius;
        const auto allowed_radius = (r - 1) * (r - 1);

        // rendering limit radius based on the horizon distance from the current camera height.
        // we use the horizon distance as a limit because tiles beyond that point won't be visible anyway,
        // so no need to even request them.
        const auto render_radius_sq = static_cast<float>(utils::calculate_horizon(position));

        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz)
                if (dz * dz + dx * dx < allowed_radius)
                    build_required(position, options.base_zoom, current_tile_x + dx, current_tile_z + dz, render_radius_sq);


        // spawn new if not in rendering list
        for (const auto &key: desired_keys)
            if (!rendering_tiles.contains(key) && !loading_tiles.contains(key))
                loading_tiles.try_emplace(key, spawn(key));
    }

    void tiles_manager::build_required(const Vector3 &position, const Zoom zoom, const int tx, const int tz, const float render_radius_sq) {
        if (zoom == options.max_zoom) {
            desired_keys.insert({zoom, tx, tz});
            return;
        }

        const auto tile = &tiles[zoom];

        // calculate distance of the tile from the camera
        const MetersDSq distance_sq = utils::distance_sq_to_tile(position, {zoom, tx, tz}, tile->size);

        // not in the area we render at all
        if (distance_sq > render_radius_sq) {
            return;
        }

        // do we need to subdivide?
        if (distance_sq >= tile->threshold) {
            desired_keys.insert({zoom, tx, tz});
            return;
        }

        const int child_zoom = zoom + 1;
        const int cx0 = tx * 2;
        const int cz0 = tz * 2;
        for (int ox = 0; ox < 2; ++ox)
            for (int oz = 0; oz < 2; ++oz) build_required(position, child_zoom, cx0 + ox, cz0 + oz, render_radius_sq);
    }

    loading_tile tiles_manager::spawn(const tile_key &tile) {
        const auto &te = tiles.at(tile.zoom);
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
        const auto &t = tiles.at(key.zoom);
        const MetersDSq distance_sq = utils::distance_sq_to_tile_xz(position, key, t.size);
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
            // long if or a loop...
            if (contains(target_zoom, child_x, child_z) &&
                contains(target_zoom, child_x + 1, child_z) &&
                contains(target_zoom, child_x + 2, child_z) &&
                contains(target_zoom, child_x + 3, child_z) &&
                contains(target_zoom, child_x, child_z + 1) &&
                contains(target_zoom, child_x + 1, child_z + 1) &&
                contains(target_zoom, child_x + 2, child_z + 1) &&
                contains(target_zoom, child_x + 3, child_z + 1) &&
                contains(target_zoom, child_x, child_z + 2) &&
                contains(target_zoom, child_x + 1, child_z + 2) &&
                contains(target_zoom, child_x + 2, child_z + 2) &&
                contains(target_zoom, child_x + 3, child_z + 2) &&
                contains(target_zoom, child_x, child_z + 3) &&
                contains(target_zoom, child_x + 1, child_z + 3) &&
                contains(target_zoom, child_x + 2, child_z + 3) &&
                contains(target_zoom, child_x + 3, child_z + 3)) {
                return true;
            }
        }

        return false;
    }
}
