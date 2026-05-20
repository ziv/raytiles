#include "../include/raytiles/raytiles.h"

#if defined(_WIN32)
#define NOGDI
#define NOUSER
#endif
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <cmath>
#include <optional>
#include <utility>

#include "raytiles/detail/utils.hpp"

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

    streamer::~streamer() = default;

    streamer::streamer(world_config world_conf,
                       streaming_config streaming_conf,
                       rendering_config rendering_conf,
                       pool_config pool_conf)
        : streaming(std::move(streaming_conf)),
          tile_renderer(rendering_conf),
          tile_manager(make_tiles_manager_options(world_conf, streaming), std::move(pool_conf)) {
        // set the rendering distance
        rlSetClipPlanes(streaming.near_plane, streaming.far_plane);
    }

    bool streamer::is_loading() const {
        return tile_manager.is_loading();
    }

    float streamer::get_loading() const {
        return tile_manager.get_loading();
    }

    renderer &streamer::get_renderer() {
        return tile_renderer;
    }

    std::optional<float> streamer::ground_height(Vector3 position) const {
        return tile_manager.ground_height(position);
    }

    void streamer::update(const Camera3D &camera) {
        const auto position = camera.position;

        tile_manager.pre_process(position);

        if (Vector2DistanceSqr({position.x, position.z}, {last_position.x, last_position.z}) > streaming.update_distance_sq ||
            std::fabs(position.y - last_position.y) > streaming.update_height) {
            last_position = position;
            tile_manager.process(position);
        }

        last_frustum = utils::extract_frustum(camera,
                                              static_cast<float>(streaming.near_plane),
                                              static_cast<float>(streaming.far_plane)
        );

        tile_manager.post_process(last_frustum);
    }

    void streamer::draw(const Camera3D &camera) {
        rendered = tile_renderer.draw(camera.position, tile_manager.make_debug_view(last_frustum));
    }

    void streamer::debug(const Camera3D &camera) {
        auto view = tile_manager.make_debug_view(last_frustum);
        renderer::debug(camera, view);
        DrawText(TextFormat("loaded=%zu  loading=%zu needed=%zu", view.rendering_tiles.size(), tile_manager.loading_count(), view.desired_keys.size()),
                 10, 10, 20, WHITE);
        DrawText(TextFormat("rendered=%d", rendered), 10, 40, 20, WHITE);
    }

    void streamer::debug_3d() {
        auto view = tile_manager.make_debug_view(last_frustum);
        renderer::debug_3d(view);
    }
} // namespace raytiles
