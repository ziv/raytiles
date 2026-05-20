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

#include "detail/renderer.h"
#include "detail/tiles_manager.h"
#include "detail/utils.hpp"

namespace raytiles {
    namespace {
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
    } // namespace

    streamer::streamer(world_config world_conf,
                       streaming_config streaming_conf,
                       rendering_config rendering_conf,
                       pool_config pool_conf)
        : streaming(std::move(streaming_conf)),
          tile_renderer(std::make_unique<renderer>(rendering_conf)),
          tile_manager(std::make_unique<tiles_manager>(make_tiles_manager_options(world_conf, streaming), std::move(pool_conf))) {
        // set the rendering distance
        rlSetClipPlanes(streaming.near_plane, streaming.far_plane);
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
        return tile_manager->ground_height(position);
    }

    void streamer::update(const Camera3D &camera) {
        const auto position = camera.position;

        tile_manager->pre_process(position);

        if (Vector2DistanceSqr({position.x, position.z}, {last_position.x, last_position.z}) > streaming.update_distance_sq ||
            std::fabs(position.y - last_position.y) > streaming.update_height) {
            last_position = position;
            tile_manager->process(position);
        }

        last_frustum = utils::extract_frustum(camera,
                                              static_cast<float>(streaming.near_plane),
                                              static_cast<float>(streaming.far_plane)
        );

        tile_manager->post_process(last_frustum);
    }

    void streamer::draw(const Camera3D &camera) {
        rendered = tile_renderer->draw(camera.position, tile_manager->make_debug_view(last_frustum));
    }

    void streamer::set_ambient_light(const Color color)                                            { tile_renderer->set_ambient_light(color); }
    void streamer::set_ambient_light(const Vector4 color)                                          { tile_renderer->set_ambient_light(color); }
    void streamer::set_ambient_light(const float r, const float g, const float b, const float a)  { tile_renderer->set_ambient_light(r, g, b, a); }
    void streamer::set_fog_color(const Color color)                                                { tile_renderer->set_fog_color(color); }
    void streamer::set_fog_color(const Vector4 color)                                              { tile_renderer->set_fog_color(color); }
    void streamer::set_fog_color(const float r, const float g, const float b, const float a)      { tile_renderer->set_fog_color(r, g, b, a); }
    void streamer::set_fog_start(const float distance)                                             { tile_renderer->set_fog_start(distance); }
    void streamer::set_fog_end(const float distance)                                               { tile_renderer->set_fog_end(distance); }
    void streamer::set_height_scale(const float scale)                                             { tile_renderer->set_height_scale(scale); }
    void streamer::set_normals_scale(const float scale)                                            { tile_renderer->set_normals_scale(scale); }
    void streamer::set_sun_direction(const Vector3 direction)                                      { tile_renderer->set_sun_direction(direction); }
    void streamer::set_sun_scale(const float scale)                                                { tile_renderer->set_sun_scale(scale); }
} // namespace raytiles
