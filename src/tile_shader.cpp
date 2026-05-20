#include "tile_shader.h"
#include "shaders.hpp"

namespace raytiles {
    tile_shader::tile_shader(const tile_shader_options &options) : shader(raii::load_shader_from_memory(shaders::vertex_shader, shaders::fragment_shader)) {
        // cache slots
        cam_pos_loc = GetShaderLocation(*shader, "cameraPosition");
        ambient_loc = GetShaderLocation(*shader, "ambientLight");
        fog_color_loc = GetShaderLocation(*shader, "fogColor");
        tex_albedo_loc = GetShaderLocation(*shader, "texture0");
        tex_height_loc = GetShaderLocation(*shader, "heightMap");
        tex_normal_loc = GetShaderLocation(*shader, "normalMap");
        sun_dir_loc = GetShaderLocation(*shader, "sunDir");
        sun_scale_loc = GetShaderLocation(*shader, "sunScale");
        height_scale_loc = GetShaderLocation(*shader, "heightScale");
        normal_scale_loc = GetShaderLocation(*shader, "normalScale");
        fog_start_loc = GetShaderLocation(*shader, "fogStart");
        fog_end_loc = GetShaderLocation(*shader, "fogEnd");
        skirt_drop = GetShaderLocation(*shader, "skirtDrop");

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
            -1 == fog_end_loc ||
            -1 == skirt_drop
        ) {
            throw std::runtime_error("failed to get shader locations");
        }

        // define the slots used with the model
        // we hack the SHADER_LOC_MAP_ROUGHNESS to be used as the heightmap input
        shader->locs[SHADER_LOC_MAP_ALBEDO] = tex_albedo_loc;
        shader->locs[SHADER_LOC_MAP_ROUGHNESS] = tex_height_loc;
        shader->locs[SHADER_LOC_MAP_NORMAL] = tex_normal_loc;

        SetShaderValue(*shader, height_scale_loc, &options.height_scale, SHADER_UNIFORM_FLOAT);
        SetShaderValue(*shader, normal_scale_loc, &options.normals_scale, SHADER_UNIFORM_FLOAT);
        SetShaderValue(*shader, fog_start_loc, &options.fog_start, SHADER_UNIFORM_FLOAT);
        SetShaderValue(*shader, fog_end_loc, &options.fog_end, SHADER_UNIFORM_FLOAT);
        SetShaderValue(*shader, sun_scale_loc, &options.sun_scale, SHADER_UNIFORM_FLOAT);
        SetShaderValue(*shader, skirt_drop, &options.skirt_drop, SHADER_UNIFORM_FLOAT);
        // set the ambient color (weather/day/night/...)
        SetShaderValue(*shader, ambient_loc, options.ambient_light, SHADER_UNIFORM_VEC4);
        // set the fog color (to match the sky)
        SetShaderValue(*shader, fog_color_loc, options.fog_color, SHADER_UNIFORM_VEC4);
        // set the sun direction
        SetShaderValue(*shader, sun_dir_loc, options.sun_direction, SHADER_UNIFORM_VEC3);
    }


    void tile_shader::set_camera_location(const Vector3 &position) {
        SetShaderValue(*shader, cam_pos_loc, &position, SHADER_UNIFORM_VEC3);
    }
}
