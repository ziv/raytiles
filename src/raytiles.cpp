#include "../include/raytiles/raytiles.h"

#if defined(_WIN32)
#define NOGDI
#define NOUSER
#endif
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <cmath>
#include <memory>
#include <optional>
#include <utility>

#include "detail/tiles_renderer.h"
#include "detail/tiles_manager.h"
#include "detail/utils.hpp"

namespace raytiles {
    namespace {
        std::pair<std::string, std::string> split_url(const std::string &url) {
            const auto scheme = url.find("://");
            if (scheme == std::string::npos) throw std::runtime_error("invalid url (no scheme): " + url);

            const auto path_pos = url.find('/', scheme + 3);
            if (path_pos == std::string::npos) return {url, "/"};

            return {url.substr(0, path_pos), url.substr(path_pos)};
        }

        // Translates the streamer's public config triplet into the
        // tiles_manager's own option struct. Mirrors `make_shader_options`
        // in renderer.cpp.
        tiles_manager_options make_tiles_manager_options(const world_config &world, const streaming_config &streaming) {
            return tiles_manager_options{
                .base_zoom = world.base_zoom,
                .max_zoom = world.max_zoom,
                .base_zoom_tile_size = world.base_zoom_tile_size,
                .anchor_x_tile = world.anchor_x_tile,
                .anchor_z_tile = world.anchor_z_tile,
                .rendering_radius = streaming.rendering_radius,
                .near_plane = streaming.near_plane,
                .far_plane = streaming.far_plane,
                .use_mipmap = world.use_mipmap,
                .upload_budget_sec = streaming.upload_budget_sec,
                .max_uploads_per_frame = streaming.max_uploads_per_frame,
                .thresholds = streaming.thresholds,
                .skirt_overlap = world.skirt_overlap,
            };
        }

        pool_options make_pool_options(const pool_config &pool_conf) {
            auto [texture_host, texture_url_path] = split_url(pool_conf.texture_url);
            auto [heightmap_host, heightmap_url_path] = split_url(pool_conf.heightmap_url);
            auto [normals_host, normals_url_path] = split_url(pool_conf.normals_url);
            return pool_options{
                .download_threads = pool_conf.download_threads,
                .allow_insecure_tls = pool_conf.allow_insecure_tls,
                .texture_cache_path = pool_conf.texture_cache_path,
                .heightmap_cache_path = pool_conf.heightmap_cache_path,
                .normals_cache_path = pool_conf.normals_cache_path,
                .texture_host = std::move(texture_host),
                .texture_url_path = std::move(texture_url_path),
                .heightmap_host = std::move(heightmap_host),
                .heightmap_url_path = std::move(heightmap_url_path),
                .normals_host = std::move(normals_host),
                .normals_url_path = std::move(normals_url_path),
            };
        }
    } // namespace

    streamer::streamer(world_config world_conf,
                       streaming_config streaming_conf,
                       rendering_config rendering_conf,
                       const pool_config &pool_conf)
        : near_plane(static_cast<float>(streaming_conf.near_plane)),
          far_plane(static_cast<float>(streaming_conf.far_plane)),
          update_distance_sq(streaming_conf.update_distance_sq),
          // streaming(std::move(streaming_conf)),
          tile_renderer(std::make_unique<tiles_renderer>(rendering_conf)),
          tile_manager(std::make_unique<tiles_manager>(make_tiles_manager_options(world_conf, streaming_conf), make_pool_options(pool_conf))) {
        // set the rendering distance
        rlSetClipPlanes(streaming_conf.near_plane, streaming_conf.far_plane);
    }

    streamer::~streamer() = default;

    streamer::streamer(streamer &&) noexcept = default;

    streamer &streamer::operator=(streamer &&) noexcept = default;

    bool streamer::is_loading() const {
        return tile_manager->is_loading();
    }

    float streamer::get_loading() const {
        return tile_manager->get_loading();
    }

    std::optional<float> streamer::ground_height(const Vector3 position) const {
        return tile_manager->ground_height(Vector3Subtract(position, cached_world_offset_));
    }

    void streamer::update(const Camera3D &camera, const Vector3 world_offset) {
        // Cache for draw() and ground_height(); they are forbidden to take
        // these as args (single source of truth = update()).
        cached_camera_ = camera;
        cached_world_offset_ = world_offset;

        // Convert camera position from user space to absolute world space.
        // Internal pipeline (tile_manager) operates in absolute space because
        // tile coordinates (tile.tx, tile.tz) are stored absolute.
        const Vector3 abs_position = Vector3Subtract(camera.position, world_offset);

        tile_manager->pre_process(abs_position);

        if (Vector3DistanceSqr(abs_position, last_position) > update_distance_sq) {
            last_position = abs_position;
            tile_manager->process(abs_position);
        }

        // Frustum is built from the user-space camera (small floats) and is
        // therefore in user space. post_process shifts each tile to user space
        // (tile.tx + offset.x) before the in-frustum test.
        last_frustum = utils::extract_frustum(camera, near_plane, far_plane);

        tile_manager->post_process(last_frustum, world_offset);
    }

    void streamer::draw() {
        rendered = tile_renderer->draw(cached_camera_.position, cached_world_offset_,
                                       tile_manager->make_debug_view(last_frustum));
    }

    void streamer::set_ambient_light(const Color color) const { tile_renderer->set_ambient_light(color); }
    void streamer::set_ambient_light(const Vector4 color) const { tile_renderer->set_ambient_light(color); }
    void streamer::set_ambient_light(const float r, const float g, const float b, const float a) const { tile_renderer->set_ambient_light(r, g, b, a); }
    void streamer::set_fog_color(const Color color) const { tile_renderer->set_fog_color(color); }
    void streamer::set_fog_color(const Vector4 color) const { tile_renderer->set_fog_color(color); }
    void streamer::set_fog_color(const float r, const float g, const float b, const float a) const { tile_renderer->set_fog_color(r, g, b, a); }
    void streamer::set_fog_start(const float distance) const { tile_renderer->set_fog_start(distance); }
    void streamer::set_fog_end(const float distance) const { tile_renderer->set_fog_end(distance); }
    void streamer::set_height_scale(const float scale) const { tile_renderer->set_height_scale(scale); }
    void streamer::set_normals_scale(const float scale) const { tile_renderer->set_normals_scale(scale); }
    void streamer::set_sun_direction(const Vector3 direction) const { tile_renderer->set_sun_direction(direction); }
    void streamer::set_sun_scale(const float scale) const { tile_renderer->set_sun_scale(scale); }
} // namespace raytiles
