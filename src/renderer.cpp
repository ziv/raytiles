#include "raytiles/raytiles.h"
#include "raytiles/detail/raii.hpp"
#include "raytiles/detail/utils.hpp"
#include "shaders.hpp"

namespace raytiles {
    renderer::renderer(const rendering_config &conf) : rendering(conf),
                                                 displacement_shader(raii::load_shader_from_memory(shaders::vertex_shader, shaders::fragment_shader)) {
        material = raii::material{LoadMaterialDefault()};
        material->shader = *displacement_shader;

        // cache shaders locations
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
        skirt_drop = GetShaderLocation(*displacement_shader, "skirtDrop");

        // validate all slots populated
        if (-1 == cam_pos_loc ||
            -1 == ambient_loc ||
            -1 == fog_color_loc ||
            -1 == tex_albedo_loc ||
            -1 == tex_height_loc ||
            -1 == tex_normal_loc ||
            -1 == sun_dir_loc ||
            -1 == sun_scale_loc ||
            -1 == height_scale_loc ||
            -1 == normal_scale_loc ||
            -1 == fog_start_loc ||
            -1 == fog_end_loc
            // -1 == skirt_drop
        ) {
            throw std::runtime_error("failed to get shader locations");
        }

        // define the slots used with the model
        // we hack the SHADER_LOC_MAP_ROUGHNESS to be used as the heightmap input
        displacement_shader->locs[SHADER_LOC_MAP_ALBEDO] = tex_albedo_loc;
        displacement_shader->locs[SHADER_LOC_MAP_ROUGHNESS] = tex_height_loc;
        displacement_shader->locs[SHADER_LOC_MAP_NORMAL] = tex_normal_loc;

        // todo should we move those into dynamically changed list? (worth the performance impact?)
        SetShaderValue(*displacement_shader, height_scale_loc, &rendering.height_scale, SHADER_UNIFORM_FLOAT);
        SetShaderValue(*displacement_shader, normal_scale_loc, &rendering.normals_scale, SHADER_UNIFORM_FLOAT);
        SetShaderValue(*displacement_shader, fog_start_loc, &rendering.fog_start, SHADER_UNIFORM_FLOAT);
        SetShaderValue(*displacement_shader, fog_end_loc, &rendering.fog_end, SHADER_UNIFORM_FLOAT);
        SetShaderValue(*displacement_shader, sun_scale_loc, &rendering.sun_scale, SHADER_UNIFORM_FLOAT);
        SetShaderValue(*displacement_shader, skirt_drop, &rendering.skirt_drop, SHADER_UNIFORM_FLOAT);

        update_shader_uniforms();
    }

    int renderer::draw(const Vector3 &position, const DebugView &draw_view) {
        int rendered = 0;
        SetShaderValue(*displacement_shader, cam_pos_loc, &position, SHADER_UNIFORM_VEC3);
        update_shader_uniforms();

        for (const auto &[key, tile]: draw_view.rendering_tiles) {
            if (const auto &t = draw_view.tiles.at(key.zoom); utils::is_tile_in_frustum(tile.tx, tile.tz, t.size, draw_view.frustum)) {
                material->maps[MATERIAL_MAP_ALBEDO].texture = *tile.tx_texture;
                material->maps[MATERIAL_MAP_ROUGHNESS].texture = *tile.hm_texture;
                material->maps[MATERIAL_MAP_NORMAL].texture = *tile.nl_texture;

                DrawMesh(*t.mesh, *material, MatrixTranslate(tile.tx, 0.0f, tile.tz));
                ++rendered;
            }
        }
        return rendered;
    }

    void renderer::debug_3d(const DebugView &draw_view) {
        for (const auto &[key, tile]: draw_view.rendering_tiles) {
            if (const auto &t = draw_view.tiles.at(key.zoom); utils::is_tile_in_frustum(tile.tx, tile.tz, t.size, draw_view.frustum)) {
                DrawCubeWires({tile.tx, 0.0f, tile.tz}, t.size, 1000.0f, t.size, GREEN);
            }
        }
    }

    void renderer::debug(const Camera3D &camera, const DebugView &draw_view) {
        const auto width = static_cast<float>(GetScreenWidth());
        const auto height = static_cast<float>(GetScreenHeight());
        for (const auto &[key, tile]: draw_view.rendering_tiles) {
            if (const auto &t = draw_view.tiles.at(key.zoom); utils::is_tile_in_frustum(tile.tx, tile.tz, t.size, draw_view.frustum)) {
                const auto [x, y] = GetWorldToScreen({tile.tx, 0.0f, tile.tz}, camera);
                if (x < 0 || x > width || y < 0 || y > height) continue;

                DrawText(TextFormat("%d", key.zoom), static_cast<int>(x), static_cast<int>(y), 15, draw_view.desired_keys.contains(key) ? GREEN : RED);
            }
        }
    }

    void renderer::update_shader_uniforms() {
        // set the ambient color (weather/day/night/...)
        SetShaderValue(*displacement_shader, ambient_loc, rendering.ambient_light, SHADER_UNIFORM_VEC4);
        // set the fog color (to match the sky)
        SetShaderValue(*displacement_shader, fog_color_loc, rendering.fog_color, SHADER_UNIFORM_VEC4);
        // set the sun direction
        SetShaderValue(*displacement_shader, sun_dir_loc, rendering.sun_direction, SHADER_UNIFORM_VEC3);
    }

    void renderer::set_ambient_light(const float r, const float g, const float b, const float a) {
        rendering.ambient_light[0] = r;
        rendering.ambient_light[1] = g;
        rendering.ambient_light[2] = b;
        rendering.ambient_light[3] = a;
        SetShaderValue(*displacement_shader, ambient_loc, rendering.ambient_light, SHADER_UNIFORM_VEC4);
    }

    void renderer::set_ambient_light(const Color color) {
        set_ambient_light(static_cast<float>(color.r) / 255.0f,
                          static_cast<float>(color.g) / 255.0f,
                          static_cast<float>(color.b) / 255.0f,
                          static_cast<float>(color.a) / 255.0f
        );
    }

    void renderer::set_ambient_light(const Vector4 color) {
        set_ambient_light(color.x, color.y, color.z, color.w);
    }

    void renderer::set_fog_color(const float r, const float g, const float b, const float a) {
        rendering.fog_color[0] = r;
        rendering.fog_color[1] = g;
        rendering.fog_color[2] = b;
        rendering.fog_color[3] = a;
        SetShaderValue(*displacement_shader, fog_color_loc, rendering.fog_color, SHADER_UNIFORM_VEC4);
    }

    void renderer::set_fog_color(const Color color) {
        set_fog_color(static_cast<float>(color.r) / 255.0f,
                      static_cast<float>(color.g) / 255.0f,
                      static_cast<float>(color.b) / 255.0f,
                      static_cast<float>(color.a) / 255.0f);
    }

    void renderer::set_fog_color(const Vector4 color) {
        set_fog_color(color.x, color.y, color.z, color.w);
    }

    void renderer::set_fog_start(const float distance) {
        rendering.fog_start = distance;
        SetShaderValue(*displacement_shader, fog_start_loc, &rendering.fog_start, SHADER_UNIFORM_FLOAT);
    }

    void renderer::set_fog_end(const float distance) {
        rendering.fog_end = distance;

        SetShaderValue(*displacement_shader, fog_end_loc, &rendering.fog_end, SHADER_UNIFORM_FLOAT);
    }

    void renderer::set_sun_direction(const Vector3 direction) {
        rendering.sun_direction[0] = direction.x;
        rendering.sun_direction[1] = direction.y;
        rendering.sun_direction[2] = direction.z;

        SetShaderValue(*displacement_shader, sun_dir_loc, rendering.sun_direction, SHADER_UNIFORM_VEC3);
    }

    void renderer::set_sun_scale(const float scale) {
        rendering.sun_scale = scale;

        SetShaderValue(*displacement_shader, sun_scale_loc, &rendering.sun_scale, SHADER_UNIFORM_FLOAT);
    }

    void renderer::set_height_scale(const float scale) {
        rendering.height_scale = scale;

        SetShaderValue(*displacement_shader, height_scale_loc, &rendering.height_scale, SHADER_UNIFORM_FLOAT);
    }

    void renderer::set_normals_scale(const float scale) {
        rendering.normals_scale = scale;

        SetShaderValue(*displacement_shader, normal_scale_loc, &rendering.normals_scale, SHADER_UNIFORM_FLOAT);
    }
}
