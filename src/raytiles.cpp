#include "../include/raytiles/raytiles.h"
#if defined(_WIN32)
#define NOGDI
#define NOUSER
#endif
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <chrono>
#include <format>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "downloader.hpp"
#include "raytiles/detail/tile.hpp"
#include "raytiles/detail/utils.hpp"
#include "shaders.hpp"

using namespace std::chrono_literals;

namespace raytiles {
    streamer::~streamer() = default;

    streamer::streamer(streamer &&) noexcept = default;

    streamer::streamer(world_config world_conf,
                       streaming_config streaming_conf,
                       rendering_config rendering_conf,
                       pool_config pool_conf)
        : near_plane(rendering_conf.near_plane), // take from rendering what we need before loosing ownership
          far_plane(rendering_conf.far_plane),
          world(std::move(world_conf)),
          streaming(std::move(streaming_conf)),
          tile_renderer(std::move(rendering_conf)),
          tile_downloader(std::make_unique<pool>(std::move(pool_conf))),
          width(static_cast<float>(GetScreenWidth())),
          height(static_cast<float>(GetScreenHeight())) {
        // input validation
        if (world.max_zoom > max_supported_zoom) {
            // stop on not supported zoom
            throw std::runtime_error(std::format("max_zoom {} is not supported; max is {}", world.max_zoom, max_supported_zoom));
        }
        if (world.base_zoom < min_tested_zoom) {
            // warn on not supported zoom
            TraceLog(LOG_WARNING, "base_zoom %d is not tested; lowest tested is %d", world.base_zoom, min_tested_zoom);
        }

        int res = min_resolution;

        for (int zoom = world.base_zoom; zoom <= world.max_zoom; ++zoom) {
            if (!streaming.thresholds.contains(zoom)) {
                // don't start with missing items
                throw std::runtime_error(std::format("missing distance threshold for zoom {}", zoom));
            }
            if (!world.skirt_overlap.contains(zoom)) {
                // don't start with missing items
                throw std::runtime_error(std::format("missing skirt_overlap for zoom {}", zoom));
            }
            const auto ratio = static_cast<float>(1 << (zoom - world.base_zoom));
            const auto size = world.base_zoom_tile_size / ratio;
            const auto th = streaming.thresholds.at(zoom); // safe (see check at the beginning of loop)
            const auto skirt_factor = world.skirt_overlap.at(zoom); // safe (see check at the beginning of loop)

            tiles[zoom] = tile_value{
                size,
                th * th,
                raii::mesh{GenMeshPlane(size * skirt_factor, size * skirt_factor, res, res)}
            };
            res = std::min(res * 2, max_resolution);
        }

        // todo should be set as part of height?! (i.e. in "process_current_location")
        // set the rendering distance
        rlSetClipPlanes(near_plane, far_plane);

        if (world.use_logger) TraceLog(LOG_INFO, "raytiles streamer initialized");
    }


    renderer &streamer::get_renderer() {
        return tile_renderer;
    }

    void streamer::update(const Camera3D &camera) {
        const auto position = camera.position;

        // start with clearing items we've done with
        remove_unused_tiles();

        // look for done futures in "desired_tiles" to build "rendered_tiles" map
        process_loaded_tiles();

        if (Vector2Distance({position.x, position.z}, {last_position.x, last_position.z}) > streaming.update_distance_sq ||
            std::fabs(position.y - last_position.y) > streaming.update_height) {
            last_position = position;

            // use current location to build "desired_tiles" map
            process_current_location();
        }

        // calculate frustum
        last_frustum = utils::extract_frustum(camera,
                                              width,
                                              height,
                                              near_plane,
                                              far_plane
        );
    }

    void streamer::draw(const Camera3D &camera) {
        rendered = tile_renderer.draw(camera.position, {last_frustum, rendering_tiles, tiles, desired_keys});
    }

    void streamer::debug(const Camera3D &camera) {
        renderer::debug(camera, {last_frustum, rendering_tiles, tiles, desired_keys});
        DrawText(TextFormat("loaded=%zu  loading=%zu needed=%zu", rendering_tiles.size(), loading_tiles.size(), desired_keys.size()), 10, 10, 20, WHITE);
        DrawText(TextFormat("rendered=%d", rendered), 10, 40, 20, WHITE);
    }

    void streamer::debug_3d() {
        renderer::debug_3d({last_frustum, rendering_tiles, tiles, desired_keys});
    }

    void streamer::remove_unused_tiles() {
        std::erase_if(rendering_tiles, [&](const auto &item) {
            // if it in desired, keep it
            if (desired_keys.contains(item.first)) return false;
            // if not in desired and not in frustum, remove without thinking
            if (!utils::is_tile_in_frustum(item.second.tx, item.second.tz, item.second.size, last_frustum)) return true;
            // if the tile is far beyond the horizon, remove without thinking
            if (is_tile_out_of_area(item.first)) return true;
            // here we stop to think
            if (!is_tile_covered(item.first)) return false;
            return true;
        });

        std::erase_if(loading_tiles, [&](const auto &item) {
            // also drop loading-tile bookkeeping for tiles we no longer want. the
            // background workers still finish their downloads and write to disk (the
            // file cache is the whole point of background streaming), but we stop
            // holding the resolved bytes in memory once they arrive.
            return !desired_keys.contains(item.first);
        });
    }

    std::optional<float> streamer::ground_height(const Vector3 position) const {
        // walk from the highest available zoom down to base; whichever zoom holds the
        // tile that contains (position.x, position.z) wins. higher zoom = finer
        // sample, so we prefer it if loaded.
        for (int zoom = world.max_zoom; zoom >= world.base_zoom; --zoom) {
            const auto &t = tiles.at(zoom);
            // const float size = tile_sizes[zoom - conf.base_zoom];
            const float size = t.size;

            const int tile_x = static_cast<int>(std::floor(position.x / size));
            const int tile_z = static_cast<int>(std::floor(position.z / size));

            const auto it = rendering_tiles.find(tile_key{zoom, tile_x, tile_z});
            if (it == rendering_tiles.end()) continue;

            const auto &tile = it->second;
            const Image &img = *tile.hm_image;
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

    void streamer::build_required(const Zoom zoom, const int tx, const int tz, const float render_radius_sq) {
        if (zoom == world.max_zoom) {
            desired_keys.insert({zoom, tx, tz});
            return;
        }

        const auto tile = &tiles[zoom];

        // calculate distance of the tile from the camera
        const MetersSq distance_sq = utils::distance_sq_to_tile(last_position, {zoom, tx, tz}, tile->size);

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
            for (int oz = 0; oz < 2; ++oz) build_required(child_zoom, cx0 + ox, cz0 + oz, render_radius_sq);
    }

    void streamer::process_current_location() {
        desired_keys.clear();
        const int current_tile_x = static_cast<int>(std::floor(last_position.x / world.base_zoom_tile_size));
        const int current_tile_z = static_cast<int>(std::floor(last_position.z / world.base_zoom_tile_size));

        // scanning radius: 10 -> is ((10 * 2 + 1) * 33km) width -> ~ 700km -> max horizon distance * 2
        const auto r = streaming.rendering_radius;
        const auto allowed_radius = (r - 1) * (r - 1);

        // rendering limit radius based on the horizon distance from the current camera height.
        // we use the horizon distance as a limit because tiles beyond that point won't be visible anyway,
        // so no need to even request them.
        const auto render_radius_sq = static_cast<float>(calculate_horizon());

        // todo set clip base on the horizon

        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz)
                if (dz * dz + dx * dx < allowed_radius)
                    build_required(world.base_zoom, current_tile_x + dx, current_tile_z + dz, render_radius_sq);


        // spawn new if not in rendering list
        for (const auto &key: desired_keys)
            if (!rendering_tiles.contains(key) && !loading_tiles.contains(key))
                loading_tiles.try_emplace(key, spawn(key));
    }

    void streamer::process_loaded_tiles() {
        // walk loading tiles; for each entry that finished downloading, either promote
        // it to rendering_tiles or drop it (invalid / no longer desired). entries are
        // erased immediately on the iterator. uploads are bounded both by a wall-clock
        // budget (to keep the frame steady on slow GPUs) and a hard cap (to keep heavy
        // single-tile uploads from running away).
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

            // futures resolved with raw file bytes - decode here on the main thread to
            // keep raylib calls off the worker threads. take const refs since the
            // shared_future may be reused by multiple consumers (we don't move-out).
            // a worker exception (network failure, fopen, etc.) propagates through
            // .get(); drop the tile so the rest of the world keeps streaming.
            const std::string *tx_bytes_ptr = nullptr;
            const std::string *hm_bytes_ptr = nullptr;
            const std::string *nm_bytes_ptr = nullptr;
            try {
                tx_bytes_ptr = &tile.tx_future.get();
                hm_bytes_ptr = &tile.hm_future.get();
                nm_bytes_ptr = &tile.nl_future.get();
                if (world.use_logger) TraceLog(LOG_DEBUG, "tile %d/%d/%d loaded", key.zoom, key.x, key.z);
            } catch (const std::exception &e) {
                TraceLog(LOG_WARNING, "tile %d/%d/%d download failed: %s - dropping", key.zoom, key.x, key.z, e.what());
                it = loading_tiles.erase(it);
                continue;
            }
            const std::string &tx_bytes = *tx_bytes_ptr;
            const std::string &hm_bytes = *hm_bytes_ptr;
            const std::string &nm_bytes = *nm_bytes_ptr;

            // take ownership of the decoded images via RAII; they unload automatically
            // on every exit path below.
            raii::image tex_img{LoadImageFromMemory(".png", reinterpret_cast<const unsigned char *>(tx_bytes.data()), static_cast<int>(tx_bytes.size()))};
            raii::image height_img{LoadImageFromMemory(".png", reinterpret_cast<const unsigned char *>(hm_bytes.data()), static_cast<int>(hm_bytes.size()))};
            raii::image normals_img{LoadImageFromMemory(".png", reinterpret_cast<const unsigned char *>(nm_bytes.data()), static_cast<int>(nm_bytes.size()))};

            // get specific error what is wrong...
            bool valid = true;
            if (!IsImageValid(*tex_img)) {
                TraceLog(LOG_WARNING, "failed to decode texture tile %d/%d/%d - dropping", key.zoom, key.x, key.z);
                valid = false;
            }
            if (!IsImageValid(*height_img)) {
                TraceLog(LOG_WARNING, "failed to decode heightmap tile %d/%d/%d - dropping", key.zoom, key.x, key.z);
                valid = false;
            }
            if (!IsImageValid(*normals_img)) {
                TraceLog(LOG_WARNING, "failed to decode normals tile %d/%d/%d - dropping", key.zoom, key.x, key.z);
                valid = false;
            }
            if (!valid) {
                it = loading_tiles.erase(it);
                continue;
            }

            // do we still need it?
            if (!desired_keys.contains(key)) {
                if (world.use_logger) TraceLog(LOG_DEBUG, "tile %d/%d/%d became stale before upload - dropping", key.zoom, key.x, key.z);
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
            if (world.use_mipmap) {
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
                                                 std::move(normals_tex)
                                             });

            it = loading_tiles.erase(it);
            ++promoted;

            if (promoted >= streaming.max_uploads_per_frame) break;
            if (GetTime() - frame_start >= streaming.upload_budget_sec) break;
        }
    }

    loading_tile streamer::spawn(const tile_key &tile) const {
        const auto &te = tiles.at(tile.zoom);
        const auto scale = 1 << (tile.zoom - world.base_zoom);
        const auto tx = tile.x + world.anchor_x_tile * scale;
        const auto tz = tile.z + world.anchor_z_tile * scale;
        const auto tile_size = te.size;

        auto t = loading_tile{
            // loading tile structure
            (static_cast<float>(tile.x) + 0.5f) * tile_size,
            (static_cast<float>(tile.z) + 0.5f) * tile_size,
            tile_downloader->enqueue_texture(tile.zoom, tx, tz),
            tile_downloader->enqueue_heightmap(tile.zoom, tx, tz),
            tile_downloader->enqueue_normals(tile.zoom, tx, tz),
        };

        if (world.use_logger) TraceLog(LOG_DEBUG, "spawned tile %d,%d,%d position %f,%f", tile.zoom, tile.x, tile.z, t.tx, t.tz);
        return t;
    }

    MetersSq streamer::calculate_horizon() const {
        const auto d = utils::horizon_ratio * std::max(last_position.y, 1.0f); // the radius of rendering
        return d * d;
    }

    bool streamer::is_tile_out_of_area(const tile_key &key) const {
        const auto &t = tiles.at(key.zoom);
        const MetersSq distance_sq = utils::distance_sq_to_tile_xz(last_position, key, t.size);
        return distance_sq > calculate_horizon();
    }

    bool streamer::is_tile_covered(const tile_key &key) const {
        const auto contains = [&](const int zoom, const int x, const int z) { return rendering_tiles.contains(tile_key{zoom, x, z}); };

        // check parent
        if (key.zoom > world.base_zoom) {
            if (contains(key.zoom - 1, key.x >> 1, key.z >> 1)) return true;
        }

        // check children
        if (key.zoom < world.max_zoom) {
            const int child_x = key.x * 2;
            const int child_z = key.z * 2;
            if (contains(key.zoom + 1, child_x, child_z) &&
                contains(key.zoom + 1, child_x + 1, child_z) &&
                contains(key.zoom + 1, child_x, child_z + 1) &&
                contains(key.zoom + 1, child_x + 1, child_z + 1)) {
                return true;
            }
        }
        return false;
    }
} // namespace raytiles
