#include "../raytiles.h"
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

#include "manager.hpp"
#include "tile.hpp"
#include "utils.hpp"
#include "shaders.hpp"

using namespace std::chrono_literals;

namespace raytiles {
    // manager

    manager::manager(const config &conf, pool_config pool_conf)
        : conf(conf),
          displacement_shader(raii::load_shader_from_memory(shaders::vertex_shader, shaders::fragment_shader)),
          tile_downloader(std::move(pool_conf)) {
        // input validation
        if (conf.max_zoom > max_supported_zoom) {
            // stop on not supported zoom
            throw std::runtime_error(std::format("max_zoom {} is not supported; max is {}", conf.max_zoom, max_supported_zoom));
        }
        if (conf.base_zoom < min_tested_zoom) {
            // warn on not supported zoom
            TraceLog(LOG_WARNING, std::format("base_zoom {} is not tested; lowest tested is {}", conf.base_zoom, min_tested_zoom).c_str());
        }

        int res = min_resolution;

        for (int zoom = conf.base_zoom; zoom <= conf.max_zoom; ++zoom) {
            const float ratio = static_cast<float>(1 << (zoom - conf.base_zoom));
            const auto size = conf.base_zoom_tile_size / ratio;
            const auto skirt_size = conf.skirt_size * ratio;
            const auto th = conf.thresholds.at(zoom);

            tiles[zoom] = TileValue{
                size,
                th * th,
                raii::mesh{GenMeshPlane(size + skirt_size, size + skirt_size, res, res)}
            };
            res = std::min(res * 2, max_resolution);
        }

        // one Material to rule them all, one material to bind them
        material = raii::material{LoadMaterialDefault()};
        material->shader = *displacement_shader;

        // update defaults
        fog_start = conf.fog_start;
        fog_end = conf.fog_end;
        height_scale = conf.height_scale;
        normals_scale = conf.normals_scale;

        // cache shader locations
        cam_pos_loc = GetShaderLocation(*displacement_shader, "cameraPosition");
        ambient_loc = GetShaderLocation(*displacement_shader, "ambientLight");
        fog_color_loc = GetShaderLocation(*displacement_shader, "fogColor");
        tex_albedo_loc = GetShaderLocation(*displacement_shader, "texture0");
        tex_height_loc = GetShaderLocation(*displacement_shader, "heightMap");
        tex_normal_loc = GetShaderLocation(*displacement_shader, "normalMap");
        sun_dir_loc = GetShaderLocation(*displacement_shader, "sunDir");
        sun_scale_loc = GetShaderLocation(*displacement_shader, "sunScale");
        height_scale_loc = GetShaderLocation(*displacement_shader, "heightScale");
        normal_scale_loc = GetShaderLocation(*displacement_shader, "normalScale");
        fog_start_loc = GetShaderLocation(*displacement_shader, "fogStart");
        fog_end_loc = GetShaderLocation(*displacement_shader, "fogEnd");


        // define the slots used with the model
        // we hack the SHADER_LOC_MAP_ROUGHNESS to be used as the heightmap input
        displacement_shader->locs[SHADER_LOC_MAP_ALBEDO] = tex_albedo_loc;
        displacement_shader->locs[SHADER_LOC_MAP_ROUGHNESS] = tex_height_loc;
        displacement_shader->locs[SHADER_LOC_MAP_NORMAL] = tex_normal_loc;

        // todo cache keys and add to "update_shader_uniforms()" too allow change those values based on camera y position
        SetShaderValue(*displacement_shader, height_scale_loc, &height_scale, SHADER_UNIFORM_FLOAT);
        SetShaderValue(*displacement_shader, normal_scale_loc, &normals_scale, SHADER_UNIFORM_FLOAT);
        SetShaderValue(*displacement_shader, fog_start_loc, &fog_start, SHADER_UNIFORM_FLOAT);
        SetShaderValue(*displacement_shader, fog_end_loc, &fog_end, SHADER_UNIFORM_FLOAT);
        SetShaderValue(*displacement_shader, sun_scale_loc, &sun_scale, SHADER_UNIFORM_FLOAT);
        // SetShaderValue(*displacement_shader, GetShaderLocation(*displacement_shader, "skirtDrop"), &conf.skirt_drop, SHADER_UNIFORM_FLOAT);

        // the reset shaders uniform (those are dynamically changed...)
        update_shader_uniforms();


        if (conf.use_logger) TraceLog(LOG_INFO, "raytiles streamer initialized");
        // todo should be set as part of height?!
        // set the rendering distance
        rlSetClipPlanes(conf.near_plane, conf.far_plane);
    }

    void manager::update(const Camera3D &camera) {
        // start with clearing items we've done with
        remove_unused_tiles();

        const auto position = camera.position;

        // look for done futures in "desired_tiles" to build "rendered_tiles" map
        process_loaded_tiles();

        // do not update tiles list if didn't pass enough distance
        if (Vector3DistanceSqr(position, last_position) < conf.update_distance && std::fabs(position.y - last_position.y) < conf.update_height) return;
        last_position = position;

        // use current location to build "desired_tiles" map
        process_current_location();
    }

    void manager::draw(const Camera3D &camera) {
        rendered = 0;
        // set the camera location (for distance -> fog)
        SetShaderValue(*displacement_shader, cam_pos_loc, &camera.position, SHADER_UNIFORM_VEC3);
        const auto f = utils::extract_frustum(camera, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight()), conf.near_plane,
                                              conf.far_plane);

        for (const auto &[key, tile]: rendering_tiles) {
            const auto &t = tiles.at(key.zoom);

            if (!utils::is_tile_in_frustum(tile.tx, tile.tz, t.size, f)) continue;

            // material->maps[0]
            // const auto m = material();
            material->maps[MATERIAL_MAP_ALBEDO].texture = *tile.tx_texture;
            material->maps[MATERIAL_MAP_ROUGHNESS].texture = *tile.hm_texture;
            material->maps[MATERIAL_MAP_NORMAL].texture = *tile.nl_texture;

            DrawMesh(*t.mesh, *material, MatrixTranslate(tile.tx, 0.0f, tile.tz));
            ++rendered;
        }
    }

    void manager::debug_3d(const Camera3D &camera) {
        // see manager::draw
        const float fwd_x = camera.target.x - camera.position.x;
        const float fwd_z = camera.target.z - camera.position.z;
        const float fwd_len = std::sqrt(fwd_x * fwd_x + fwd_z * fwd_z);
        const bool cull_enabled = fwd_len > 0.0001f;
        const float fwd_nx = cull_enabled ? fwd_x / fwd_len : 0.0f;
        const float fwd_nz = cull_enabled ? fwd_z / fwd_len : 0.0f;

        for (const auto &[key, tile]: rendering_tiles) {
            const auto &t = tiles.at(key.zoom);
            if (cull_enabled) {
                const float dx = tile.tx - camera.position.x;
                const float dz = tile.tz - camera.position.z;
                const float along_forward = dx * fwd_nx + dz * fwd_nz;
                if (along_forward < -t.size) continue;
            }
            DrawCubeWires({tile.tx, 0.0f, tile.tz}, t.size, 200.0f, t.size, GREEN);
        }
    }

    void manager::debug(const Camera3D &camera) {
        const auto width = static_cast<float>(GetScreenWidth());
        const auto height = static_cast<float>(GetScreenHeight());
        for (const auto &[key, tile]: rendering_tiles) {
            const auto [x, y] = GetWorldToScreen({tile.tx, 0.0f, tile.tz}, camera);
            if (x < 0 || x > width || y < 0 || y > height) continue;

            if (desired_keys.contains(key)) {
                DrawText(TextFormat("%d", key.zoom), static_cast<int>(x), static_cast<int>(y), 15, GREEN);
            } else {
                DrawText(TextFormat("%d", key.zoom), static_cast<int>(x), static_cast<int>(y), 15, RED);
            }
            // const Color c = key.zoom == 14 ? RED : key.zoom == 15 ? GREEN : WHITE;
            // DrawText(TextFormat("%d", key.zoom), static_cast<int>(x), static_cast<int>(y), 15, GREEN);
        }
        DrawText(TextFormat("loaded=%zu  loading=%zu needed=%zu", rendering_tiles.size(), loading_tiles.size(), desired_keys.size()), 10, 10, 20, WHITE);
        DrawText(TextFormat("rendered=%d", rendered), 10, 40, 20, WHITE);
    }

    void manager::remove_unused_tiles() {
        std::erase_if(rendering_tiles, [&](const auto &item) {
            if (desired_keys.contains(item.first)) return false;
            if (is_tile_out_of_area(item.first)) return true;
            if (!is_tile_covered(item.first)) return false;
            // if (!is_tile_out_of_area(item.first, item.second)) return false;
            return true;
        });
        // also drop loading-tile bookkeeping for tiles we no longer want. the
        // background workers still finish their downloads and write to disk (the
        // file cache is the whole point of background streaming), but we stop
        // holding the resolved bytes in memory once they arrive.
        std::erase_if(loading_tiles, [&](const auto &item) { return !desired_keys.contains(item.first); });
    }


    void manager::set_ambient_light(float r, float g, float b, float a) {
        ambient_light[0] = r;
        ambient_light[1] = g;
        ambient_light[2] = b;
        ambient_light[3] = a;
        SetShaderValue(*displacement_shader, ambient_loc, ambient_light, SHADER_UNIFORM_VEC4);
    }

    void manager::set_ambient_light(const Color color) {
        set_ambient_light(static_cast<float>(color.r) / 255.0f, static_cast<float>(color.g) / 255.0f, static_cast<float>(color.b) / 255.0f,
                          static_cast<float>(color.a) / 255.0f);
    }

    void manager::set_ambient_light(Vector4 color) {
        set_ambient_light(color.x, color.y, color.z, color.w);
    }

    void manager::set_fog_color(const float r, const float g, const float b, const float a) {
        fog_color[0] = r;
        fog_color[1] = g;
        fog_color[2] = b;
        fog_color[3] = a;
        SetShaderValue(*displacement_shader, fog_color_loc, fog_color, SHADER_UNIFORM_VEC4);
    }

    void manager::set_fog_color(const Color color) {
        set_fog_color(static_cast<float>(color.r) / 255.0f, static_cast<float>(color.g) / 255.0f, static_cast<float>(color.b) / 255.0f,
                      static_cast<float>(color.a) / 255.0f);
    }

    void manager::set_fog_color(const Vector4 color) {
        set_fog_color(color.x, color.y, color.z, color.w);
    }


    void manager::set_fog_start(const float distance) {
        fog_start = distance;
        SetShaderValue(*displacement_shader, fog_start_loc, &fog_start, SHADER_UNIFORM_FLOAT);
    }

    void manager::set_fog_end(const float distance) {
        fog_end = distance;

        SetShaderValue(*displacement_shader, fog_end_loc, &fog_end, SHADER_UNIFORM_FLOAT);
    }

    void manager::set_sun_direction(Vector3 direction) {
        sun_direction[0] = direction.x;
        sun_direction[1] = direction.y;
        sun_direction[2] = direction.z;

        SetShaderValue(*displacement_shader, sun_dir_loc, sun_direction, SHADER_UNIFORM_VEC3);
    }

    void manager::set_sun_scale(const float scale) {
        sun_scale = scale;

        SetShaderValue(*displacement_shader, sun_scale_loc, &sun_scale, SHADER_UNIFORM_FLOAT);
    }

    void manager::set_height_scale(const float scale) {
        height_scale = scale;

        SetShaderValue(*displacement_shader, height_scale_loc, &height_scale, SHADER_UNIFORM_FLOAT);
    }

    void manager::set_normals_scale(const float scale) {
        normals_scale = scale;

        SetShaderValue(*displacement_shader, normal_scale_loc, &normals_scale, SHADER_UNIFORM_FLOAT);
    }

    std::optional<float> manager::ground_height(const Vector3 position) const {
        // walk from the highest available zoom down to base; whichever zoom holds the
        // tile that contains (position.x, position.z) wins. higher zoom = finer
        // sample, so we prefer it if loaded.
        for (int zoom = conf.max_zoom; zoom >= conf.base_zoom; --zoom) {
            const auto &t = tiles.at(zoom);
            // const float size = tile_sizes[zoom - conf.base_zoom];
            const float size = t.size;

            const int tile_x = static_cast<int>(std::floor(position.x / size));
            const int tile_z = static_cast<int>(std::floor(position.z / size));

            const auto it = rendering_tiles.find(TileKey{zoom, tile_x, tile_z});
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

    void manager::build_required(const int zoom, const int tx, const int tz, const float render_radius_sq) {
        if (zoom == conf.max_zoom) {
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

    void manager::process_current_location() {
        desired_keys.clear();
        const int current_tile_x = static_cast<int>(std::floor(last_position.x / conf.base_zoom_tile_size));
        const int current_tile_z = static_cast<int>(std::floor(last_position.z / conf.base_zoom_tile_size));

        // scanning radius: 10 -> is ((10 * 2 + 1) * 33km) width -> ~ 700km -> max horizon distance * 2
        const auto r = conf.rendering_radius;
        const auto allowed_radius = (r - 1) * (r - 1);

        // rendering limit radius based on the horizon distance from the current camera height.
        // we use the horizon distance as a limit because tiles beyond that point won't be visible anyway,
        // so no need to even request them.
        const auto height = std::max(last_position.y, 1.0f);
        const auto d = 3.57 * 1000 * height; // the radius of rendering

        // todo set clip base on the horizon

        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz)
                if (dz * dz + dx * dx < allowed_radius)
                    build_required(conf.base_zoom, current_tile_x + dx, current_tile_z + dz, d * d);


        // spawn new if not in rendering list
        for (const auto &key: desired_keys)
            if (!rendering_tiles.contains(key) && !loading_tiles.contains(key))
                loading_tiles.try_emplace(key, spawn(key));
    }

    void manager::process_loaded_tiles() {
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
                if (conf.use_logger) TraceLog(LOG_DEBUG, "tile %d/%d/%d loaded", key.zoom, key.x, key.z);
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
                if (conf.use_logger) TraceLog(LOG_DEBUG, "tile %d/%d/%d became stale before upload - dropping", key.zoom, key.x, key.z);
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
            if (conf.use_mipmap) {
                GenTextureMipmaps(&texture_tex.get());
                SetTextureFilter(*texture_tex, TEXTURE_FILTER_ANISOTROPIC_16X);
            }

            rendering_tiles.insert_or_assign(key, loaded_tile{
                                                 tile.tx,
                                                 tile.tz,
                                                 std::move(texture_tex),
                                                 std::move(height_tex),
                                                 std::move(height_img),
                                                 std::move(normals_tex)
                                             });

            it = loading_tiles.erase(it);
            ++promoted;

            if (promoted >= conf.max_uploads_per_frame) break;
            if (GetTime() - frame_start >= conf.upload_budget_sec) break;
        }
    }

    loading_tile manager::spawn(const TileKey &tile) {
        const auto &te = tiles.at(tile.zoom);
        const auto scale = 1 << (tile.zoom - conf.base_zoom);
        const auto tx = tile.x + conf.anchor_x_tile * scale;
        const auto tz = tile.z + conf.anchor_z_tile * scale;
        const auto tile_size = te.size;

        auto t = loading_tile{
            // loading tile structure
            (static_cast<float>(tile.x) + 0.5f) * tile_size,
            (static_cast<float>(tile.z) + 0.5f) * tile_size,
            tile_downloader.enqueue_texture(tile.zoom, tx, tz),
            tile_downloader.enqueue_heightmap(tile.zoom, tx, tz),
            tile_downloader.enqueue_normals(tile.zoom, tx, tz),
        };

        if (conf.use_logger) TraceLog(LOG_DEBUG, "spawned tile %d,%d,%d position %f,%f", tile.zoom, tile.x, tile.z, t.tx, t.tz);
        return t;
    }

    void manager::update_shader_uniforms() {
        // set the ambient color (weather/day/night/...)
        SetShaderValue(*displacement_shader, ambient_loc, ambient_light, SHADER_UNIFORM_VEC4);
        // set the fog color (to match the sky)
        SetShaderValue(*displacement_shader, fog_color_loc, fog_color, SHADER_UNIFORM_VEC4);
        // set the sun direction
        SetShaderValue(*displacement_shader, sun_dir_loc, sun_direction, SHADER_UNIFORM_VEC3);
    }

    // todo check this function
    bool manager::is_tile_out_of_area(const TileKey &key) const {
        const auto &t = tiles.at(key.zoom);
        const MetersSq distance_sq = utils::distance_sq_to_tile(last_position, key, t.size);
        const MetersSq far_sq = static_cast<double>(conf.fog_end) * static_cast<double>(conf.fog_end);

        if (distance_sq > far_sq) {
            return true;
        }
        return false;
    }

    bool manager::is_tile_covered(const TileKey &key) const {
        const auto contains = [&](const int zoom, const int x, const int z) { return rendering_tiles.contains(TileKey{zoom, x, z}); };

        // check parent
        if (key.zoom > conf.base_zoom) {
            if (contains(key.zoom - 1, key.x >> 1, key.z >> 1)) return true;
        }

        // check children. use multiplication instead of left-shift: signed left-shift
        // on negative tile indices (which we have around the anchor) is UB even in
        // C++20.
        if (key.zoom < conf.max_zoom) {
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

    // streamer (pImpl forwarding)

    streamer::streamer(const config &conf, const pool_config &pool_conf) : impl(
        std::make_unique<manager>(conf, pool_conf)) {
    }

    streamer::~streamer() = default;

    streamer::streamer(streamer &&) noexcept = default;

    streamer &streamer::operator=(streamer &&) noexcept = default;

    void streamer::update(const Camera3D &camera) { impl->update(camera); }
    void streamer::draw(const Camera3D &camera) const { impl->draw(camera); }
    void streamer::debug(const Camera3D &camera) const { impl->debug(camera); }
    void streamer::debug_3d(const Camera3D &camera) const { impl->debug_3d(camera); }
    void streamer::set_ambient_light(const Color color) const { impl->set_ambient_light(color); }
    void streamer::set_fog_color(const Color color) const { impl->set_fog_color(color); }
    void streamer::set_sun_direction(const Vector3 direction) const { impl->set_sun_direction(direction); }
    void streamer::set_sun_scale(const float scale) const { impl->set_sun_scale(scale); }
    void streamer::set_fog_start(const float distance) const { impl->set_fog_start(distance); }
    void streamer::set_fog_end(const float distance) const { impl->set_fog_end(distance); }
    void streamer::set_height_scale(const float scale) const { impl->set_height_scale(scale); }
    void streamer::set_normals_scale(const float scale) const { impl->set_normals_scale(scale); }
    std::optional<float> streamer::ground_height(const Vector3 position) const { return impl->ground_height(position); }
} // namespace raytiles
