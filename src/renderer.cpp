#include <algorithm>

#include "raytiles/raytiles.h"
#include "detail/renderer.h"
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

    renderer::renderer(const rendering_config &conf) : shader_(make_shader_options(conf)) {
        material = raii::material{LoadMaterialDefault()};
        material->shader = shader_();
    }

    int renderer::draw(const Vector3 &position, const DebugView &draw_view) {
        shader_.set_camera_location(position);

        // collecting and sorting tiles by distance from camera to gain GPU early-Z
        // it cheaper than rendering by iterating unordered map -> poor early-Z
        draw_order_.clear();
        draw_order_.reserve(draw_view.rendering_tiles.size());

        for (const auto &[key, tile]: draw_view.rendering_tiles) {
            if (!tile.in_frustum_this_frame) continue;
            const auto it = draw_view.tiles.find(key.zoom);

            const float dx = tile.tx - position.x;
            const float dz = tile.tz - position.z;
            const float dist_sq = dx * dx + dz * dz; // XZ is enough; ignore Y for sorting
            draw_order_.push_back({dist_sq, &key, &tile, &it->second});
        }

        std::ranges::sort(draw_order_,
                          [](const auto &a, const auto &b) { return a.dist_sq < b.dist_sq; });

        int rendered = 0;
        for (const auto &e: draw_order_) {
            material->maps[MATERIAL_MAP_ALBEDO].texture = *e.tile->tx_texture;
            material->maps[MATERIAL_MAP_ROUGHNESS].texture = *e.tile->hm_texture;
            material->maps[MATERIAL_MAP_NORMAL].texture = *e.tile->nl_texture;
            DrawMesh(*e.tv->mesh, *material, MatrixTranslate(e.tile->tx, 0.0f, e.tile->tz));
            ++rendered;
        }
        return rendered;
    }

    void renderer::debug_3d(const DebugView &draw_view) {
        for (const auto &[key, tile]: draw_view.rendering_tiles) {
            if (tile.in_frustum_this_frame) {
                const auto &t = draw_view.tiles.at(key.zoom);
                DrawCubeWires({tile.tx, 0.0f, tile.tz}, t.size, 1000.0f, t.size, GREEN);
            }
        }
    }

    void renderer::debug(const Camera3D &camera, const DebugView &draw_view) {
        const auto width = static_cast<float>(GetScreenWidth());
        const auto height = static_cast<float>(GetScreenHeight());
        for (const auto &[key, tile]: draw_view.rendering_tiles) {
            if (tile.in_frustum_this_frame) {
                const auto [x, y] = GetWorldToScreen({tile.tx, 0.0f, tile.tz}, camera);
                if (x < 0 || x > width || y < 0 || y > height) continue;

                DrawText(TextFormat("%d", key.zoom), static_cast<int>(x), static_cast<int>(y), 15, draw_view.desired_keys.contains(key) ? GREEN : RED);
            }
        }
    }

    void renderer::set_ambient_light(const float r, const float g, const float b, const float a) {
        shader_.set_ambient_light(r, g, b, a);
    }

    void renderer::set_ambient_light(const Color color) {
        shader_.set_ambient_light(color);
    }

    void renderer::set_ambient_light(const Vector4 color) {
        shader_.set_ambient_light(color);
    }

    void renderer::set_fog_color(const float r, const float g, const float b, const float a) {
        shader_.set_fog_color(r, g, b, a);
    }

    void renderer::set_fog_color(const Color color) {
        shader_.set_fog_color(color);
    }

    void renderer::set_fog_color(const Vector4 color) {
        shader_.set_fog_color(color);
    }

    void renderer::set_fog_start(const float distance) {
        shader_.set_fog_start(distance);
    }

    void renderer::set_fog_end(const float distance) {
        shader_.set_fog_end(distance);
    }

    void renderer::set_sun_direction(const Vector3 direction) {
        shader_.set_sun_direction(direction);
    }

    void renderer::set_sun_scale(const float scale) {
        shader_.set_sun_scale(scale);
    }

    void renderer::set_height_scale(const float scale) {
        shader_.set_height_scale(scale);
    }

    void renderer::set_normals_scale(const float scale) {
        shader_.set_normals_scale(scale);
    }
}
