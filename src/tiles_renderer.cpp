#include <span>

#include "raytiles/raytiles.h"
#include "detail/tiles_renderer.h"
#include "detail/raii.hpp"
#include "detail/tile_shader.h"

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

    int tiles_renderer::draw(const Vector3 &camera_position, const std::span<const render_item> items) {
        // camera_position is in user space; the shader fragment-distance term
        // lives in user space as well (vertices are submitted with the baked
        // user-space transform), so this is the correct frame.
        shader_.set_camera_location(camera_position);

        int rendered = 0;
        for (const auto &item: items) {
            if (!item.visible) continue;

            material->maps[MATERIAL_MAP_ALBEDO].texture = item.albedo;
            material->maps[MATERIAL_MAP_ROUGHNESS].texture = item.heightmap;
            material->maps[MATERIAL_MAP_NORMAL].texture = item.normals;

            DrawMesh(item.mesh, *material, item.transform);
            ++rendered;
        }
        return rendered;
    }

    void tiles_renderer::debug_3d(const std::span<const render_item> items) {
        for (const auto &item: items) {
            if (item.visible) {
                DrawCubeWires({item.transform.m12, 0.0f, item.transform.m14}, item.size, 1000.0f, item.size, GREEN);
            }
        }
    }

    void tiles_renderer::debug(const Camera3D &camera, const std::span<const render_item> items) {
        const auto width = static_cast<float>(GetScreenWidth());
        const auto height = static_cast<float>(GetScreenHeight());
        for (const auto &item: items) {
            if (item.visible) {
                const auto [x, y] = GetWorldToScreen({item.transform.m12, 0.0f, item.transform.m14}, camera);
                if (x < 0 || x > width || y < 0 || y > height) continue;

                DrawText(TextFormat("%d", item.key.zoom), static_cast<int>(x), static_cast<int>(y), 15, item.desired ? GREEN : RED);
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
