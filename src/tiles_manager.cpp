#include "raytiles/raytiles.h"
#include "detail/tiles_manager.h"
#include <format>
#include <ranges>
#include <utility>
#include <unordered_map>
#include <vector>
#include "detail/utils.hpp"

namespace raytiles {
    tiles_manager::tiles_manager(const tiles_manager_options &opts, source_options src_opts)
        : options(opts),
          lod_options{
              .base_zoom = opts.base_zoom,
              .max_zoom = opts.max_zoom,
              .base_tile_size = opts.base_zoom_tile_size,
              .radius = opts.rendering_radius,
              .thresholds = opts.thresholds,
          },
          source(std::move(src_opts)) {
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

    float tiles_manager::get_loading() const {
        if (loading_keys.empty()) return 0.0f;
        const auto required = static_cast<float>(desired_keys.size());
        if (required == 0.0f) return 0.0f; // avoid division by zero, should not happen but just in case
        const auto outstanding = static_cast<float>(loading_keys.size());
        return 1 - outstanding / required;
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

        // stop loading tiles that fell out of the desired set. cancellation
        // is best-effort: a payload that slips through anyway is dropped by
        // the desired-set check in process_loaded_tiles.
        for (auto it = loading_keys.begin(); it != loading_keys.end();) {
            if (!desired_keys.contains(*it)) {
                source.cancel(*it);
                it = loading_keys.erase(it);
            } else {
                ++it;
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
            const auto user_x = static_cast<float>(tile.tx + static_cast<double>(world_offset.x));
            const auto user_z = static_cast<float>(tile.tz + static_cast<double>(world_offset.z));
            tile.in_frustum_this_frame = utils::is_tile_in_frustum(user_x, user_z, tile.size, frustum);
        }
        // first time nothing is loading or pending upload, initial load is done
        if (loading && loading_keys.empty()) {
            loading = false;
        }
    }

    data_view tiles_manager::make_debug_view(Frustum &frustum) {
        return data_view{frustum, rendering_tiles, tiles, desired_keys};
    }

    void tiles_manager::process_loaded_tiles() {
        // a failed tile (network / decode error, already logged by the
        // worker) is simply forgotten; a later desired-set rebuild may
        // request it again.
        for (const auto &key: source.drain_failures()) loading_keys.erase(key);

        // collect payloads completed since last frame. moves only — the
        // decoded pixel buffers keep their single owner all the way from the
        // worker to the GPU upload below.
        for (auto &&payload: source.drain()) pending_uploads.push_back(std::move(payload));

        // promote pending payloads into rendering_tiles. uploads are bounded
        // both by a wall-clock budget (to keep the frame steady on slow GPUs)
        // and a hard cap (to keep heavy single-tile uploads from running
        // away). PNG decode already happened off-thread, so this loop only
        // does GPU upload + bookkeeping.
        const double frame_start = GetTime();
        int promoted = 0;

        while (!pending_uploads.empty()) {
            tile_payload payload = std::move(pending_uploads.back());
            pending_uploads.pop_back();
            const tile_key key = payload.key;
            loading_keys.erase(key);

            // do we still need it? (dropping frees the CPU images here)
            if (!desired_keys.contains(key)) continue;

            // upload to GPU and move into rendering_tiles. the heightmap CPU image is
            // kept in the loaded_tile for ground_height() queries (recast, collision).
            raii::texture texture_tex = raii::load_texture_from_image(*payload.albedo);
            raii::texture height_tex = raii::load_texture_from_image(*payload.height);
            raii::texture normals_tex = raii::load_texture_from_image(*payload.normals);

            // don't clamp the ends
            SetTextureWrap(*texture_tex, TEXTURE_WRAP_CLAMP);
            SetTextureWrap(*height_tex, TEXTURE_WRAP_CLAMP);
            SetTextureWrap(*normals_tex, TEXTURE_WRAP_CLAMP);

            // allow use of mipmaps (only for texture)
            if (options.use_mipmap) {
                GenTextureMipmaps(&texture_tex.get());
                SetTextureFilter(*texture_tex, TEXTURE_FILTER_ANISOTROPIC_16X);
            }

            // world-space tile center, matching the mesh translation in draw
            const auto tile_size = tiles.at(key.zoom).size;
            const auto tx = (static_cast<double>(key.x) + 0.5) * static_cast<double>(tile_size);
            const auto tz = (static_cast<double>(key.z) + 0.5) * static_cast<double>(tile_size);

            rendering_tiles.insert_or_assign(key, loaded_tile{
                                                 tile_size,
                                                 tx,
                                                 tz,
                                                 std::move(texture_tex),
                                                 std::move(height_tex),
                                                 std::move(payload.height),
                                                 std::move(normals_tex),
                                                 false
                                             });

            ++promoted;

            if (promoted >= options.max_uploads_per_frame) break;
            if (GetTime() - frame_start >= options.upload_budget_sec) break;
        }
    }

    void tiles_manager::process_current_location(const Vector3 &position) {
        // the desired-set policy is a pure function (see lod.hpp); this method
        // only owns the side effect of spawning downloads for missing tiles.
        lod::desired_tiles(lod_options, position, desired_keys);

        // request whatever is neither resident nor already loading
        for (const auto &key: desired_keys)
            if (!rendering_tiles.contains(key) && !loading_keys.contains(key))
                request(key);
    }

    void tiles_manager::request(const tile_key &key) {
        // tile keys are anchor-relative; the provider wants absolute slippy
        // coordinates, so shift by the anchor scaled to this key's zoom.
        const auto scale = 1 << (key.zoom - options.base_zoom);
        source.request(key, key.x + options.anchor_x_tile * scale, key.z + options.anchor_z_tile * scale);
        loading_keys.insert(key);
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
            for (int ox = 0; ox < 4; ++ox)
                for (int oz = 0; oz < 4; ++oz)
                    if (!contains(target_zoom, child_x + ox, child_z + oz))
                        return false;
            return true;
        }

        return false;
    }
}
