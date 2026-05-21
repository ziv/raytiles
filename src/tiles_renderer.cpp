#include <algorithm>

#include "raytiles/raytiles.h"
#include "detail/tiles_renderer.h"
#include "detail/raii.hpp"
#include "detail/tile_shader.h"
#include "detail/utils.hpp"

namespace raytiles {
    namespace {
        // Translate `rendering_config` (public API) into `tile_shader_options`
        // (shader-side mirror). Field-for-field copy.
        tile_shader_options make_shader_options(const rendering_config &conf) {
            tile_shader_options opts;
            opts.fog_start = conf.fog_start;
            opts.fog_end = conf.fog_end;
            opts.skirt_drop = conf.skirt_drop;
            for (int i = 0; i < 4; ++i) opts.fog_color[i] = conf.fog_color[i];
            for (int i = 0; i < 4; ++i) opts.ambient_light[i] = conf.ambient_light[i];
            for (int i = 0; i < 3; ++i) opts.sun_direction[i] = conf.sun_direction[i];
            opts.sun_scale = conf.sun_scale;
            opts.height_scale = conf.height_scale;
            opts.normals_scale = conf.normals_scale;
            return opts;
        }
    }

    tiles_renderer::tiles_renderer(const rendering_config &conf) : shader_(make_shader_options(conf)) {
        material = raii::material{LoadMaterialDefault()};
        material->shader = shader_();
    }

    int tiles_renderer::draw(const Vector3 &position, const Vector3 &world_offset, const data_view &draw_view) {
        // position is in user space; the shader fragment-distance term lives
        // in user space as well (vertices are submitted post-MatrixTranslate
        // with user-space coords), so this is the correct frame.
        shader_.set_camera_location(position);

        const auto off_x = static_cast<double>(world_offset.x);
        const auto off_z = static_cast<double>(world_offset.z);

        int rendered = 0;
        for (auto &[key, tile]: draw_view.rendering_tiles) {
            if (!tile.in_frustum_this_frame) continue;
            const auto it = draw_view.tiles.find(key.zoom);

            material->maps[MATERIAL_MAP_ALBEDO].texture = *tile.tx_texture;
            material->maps[MATERIAL_MAP_ROUGHNESS].texture = *tile.hm_texture;
            material->maps[MATERIAL_MAP_NORMAL].texture = *tile.nl_texture;

            // Convert absolute tile center into user space for raylib's
            // float-only matrix pipeline. Keep the addition in double so the
            // huge-tile-coord + huge-offset cancellation happens at full
            // precision before the float cast.
            const auto user_tx = static_cast<float>(tile.tx + off_x);
            const auto user_tz = static_cast<float>(tile.tz + off_z);
            DrawMesh(*it->second.mesh, *material, MatrixTranslate(user_tx, 0.0f, user_tz));
            ++rendered;
        }
        return rendered;
    }

    void tiles_renderer::debug_3d(const Vector3 &world_offset, const data_view &draw_view) {
        const auto off_x = static_cast<double>(world_offset.x);
        const auto off_z = static_cast<double>(world_offset.z);
        for (const auto &[key, tile]: draw_view.rendering_tiles) {
            if (tile.in_frustum_this_frame) {
                const auto &t = draw_view.tiles.at(key.zoom);
                const auto user_x = static_cast<float>(tile.tx + off_x);
                const auto user_z = static_cast<float>(tile.tz + off_z);
                DrawCubeWires({user_x, 0.0f, user_z}, t.size, 1000.0f, t.size, GREEN);
            }
        }
    }

    void tiles_renderer::debug(const Camera3D &camera, const Vector3 &world_offset, const data_view &draw_view) {
        const auto width = static_cast<float>(GetScreenWidth());
        const auto height = static_cast<float>(GetScreenHeight());
        const auto off_x = static_cast<double>(world_offset.x);
        const auto off_z = static_cast<double>(world_offset.z);
        for (const auto &[key, tile]: draw_view.rendering_tiles) {
            if (tile.in_frustum_this_frame) {
                const auto user_x = static_cast<float>(tile.tx + off_x);
                const auto user_z = static_cast<float>(tile.tz + off_z);
                const auto [x, y] = GetWorldToScreen({user_x, 0.0f, user_z}, camera);
                if (x < 0 || x > width || y < 0 || y > height) continue;

                DrawText(TextFormat("%d", key.zoom), static_cast<int>(x), static_cast<int>(y), 15, draw_view.desired_keys.contains(key) ? GREEN : RED);
            }
        }
    }

    void tiles_renderer::set_ambient_light(const float r, const float g, const float b, const float a) {
        shader_.set_ambient_light(r, g, b, a);
    }

    void tiles_renderer::set_ambient_light(const Color color) {
        shader_.set_ambient_light(color);
    }

    void tiles_renderer::set_ambient_light(const Vector4 color) {
        shader_.set_ambient_light(color);
    }

    void tiles_renderer::set_fog_color(const float r, const float g, const float b, const float a) {
        shader_.set_fog_color(r, g, b, a);
    }

    void tiles_renderer::set_fog_color(const Color color) {
        shader_.set_fog_color(color);
    }

    void tiles_renderer::set_fog_color(const Vector4 color) {
        shader_.set_fog_color(color);
    }

    void tiles_renderer::set_fog_start(const float distance) {
        shader_.set_fog_start(distance);
    }

    void tiles_renderer::set_fog_end(const float distance) {
        shader_.set_fog_end(distance);
    }

    void tiles_renderer::set_sun_direction(const Vector3 direction) {
        shader_.set_sun_direction(direction);
    }

    void tiles_renderer::set_sun_scale(const float scale) {
        shader_.set_sun_scale(scale);
    }

    void tiles_renderer::set_height_scale(const float scale) {
        shader_.set_height_scale(scale);
    }

    void tiles_renderer::set_normals_scale(const float scale) {
        shader_.set_normals_scale(scale);
    }
}
