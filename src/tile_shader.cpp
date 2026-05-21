#include "raytiles/raytiles.h"
#include "detail/tile_shader.h"

#ifdef __EMSCRIPTEN__
#define GLSL_VERSION_HEADER "#version 300 es\nprecision mediump float;\n"
#else
#define GLSL_VERSION_HEADER "#version 330\n"
#endif

namespace raytiles {
    namespace {
        // language=GLSL
        constexpr auto vertex_shader = GLSL_VERSION_HEADER R"glsl(
in vec3 vertexPosition;         // current vertex position
in vec2 vertexTexCoord;         // current vertex uv

uniform mat4 mvp;               // model view projection
uniform mat4 matModel;          // position matrix
uniform sampler2D heightMap;    // heightmap input
uniform float heightScale;      // scale factor input
uniform vec3 cameraPosition;    // camera world position (for distance -> fog)
uniform float skirtDrop;

out vec2 fragTexCoord;          // uv for the fragment
out float fragCamDist;          // distance from the camera to the vertex, for fog calculation in the fragment shader

void main()
{
    // moving the value to the fragment
    fragTexCoord = vertexTexCoord;

    // terrarium heightmap calculations
    vec3 color = texture(heightMap, vertexTexCoord).rgb;
    vec3 c = color * 255.0;
    float heightValue = (c.r * 256.0 + c.g + c.b / 256.0) - 32768.0;

    // update the displacement
    vec3 displacedPosition = vertexPosition;
    displacedPosition.y += heightValue * heightScale;

    // if the pixel is on edge, push it down by a fixed amount to create a skirt effect
    float epsilon = 0.000001;
    float isLeftEdge = step(vertexTexCoord.x, epsilon);
    float isRightEdge = step(1.0 - epsilon, vertexTexCoord.x);
    float isBottomEdge = step(vertexTexCoord.y, epsilon);
    float isTopEdge = step(1.0 - epsilon, vertexTexCoord.y);
    float edgeFactor = clamp(isLeftEdge + isRightEdge + isBottomEdge + isTopEdge, 0.0, 1.0);
    displacedPosition.y -= skirtDrop * heightScale * edgeFactor;

    // calculate wold position with the displacement
    vec3 worldPosition = vec3(matModel * vec4(displacedPosition, 1.0));

    // send the camera distance to the fragment
    fragCamDist = distance(worldPosition, cameraPosition);

    // real pixel position on screen with displacement
    gl_Position = mvp * vec4(displacedPosition, 1.0);
}
)glsl";

        // language=GLSL
        constexpr auto fragment_shader = GLSL_VERSION_HEADER R"glsl(
in vec2 fragTexCoord;
in float fragCamDist;

uniform sampler2D texture0;     // default input texture
uniform sampler2D normalMap;    // normal map input
uniform vec4 ambientLight;      // ambient color input
uniform vec4 fogColor;          // for color (should match sky color)
uniform float fogStart;         // distance from the camera where fog starts (should be tuned with the tile size and height scale)
uniform float fogEnd;           // distance from the camera where fog is fully opaque (should be tuned with the tile size and height scale)
uniform vec3 sunDir;            // light direction
uniform float normalScale;      // scale the normals to control the contrast
uniform float sunScale;         // scale the sun light intensity

out vec4 finalColor;

void main()
{
    vec4 texColor = texture(texture0, fragTexCoord);
    vec3 normalColor = texture(normalMap, fragTexCoord).rgb;

    // increasing normals (use scale uniform like height)
    vec3 normal = normalColor * 2.0 - 1.0;
    normal.xy *= normalScale;
    normal = normalize(normal);

    // sun light factor should be a uniform
    float sunLight = max(dot(normal, normalize(sunDir)), 0.0);
    vec4 sunColor = vec4(sunLight * sunScale);

    vec4 totalLighting = ambientLight + sunColor;
    totalLighting = clamp(totalLighting, 0.0, 1.0);
    vec4 lit = texColor * totalLighting;

    float fogFactor = clamp((fragCamDist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    finalColor = mix(lit, fogColor, fogFactor);
}
)glsl";
    }

    tile_shader::tile_shader(const tile_shader_options &opts)
        : options(opts),
          shader(raii::load_shader_from_memory(vertex_shader, fragment_shader)) {
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


    tile_shader &tile_shader::set_camera_location(const Vector3 &position) {
        SetShaderValue(*shader, cam_pos_loc, &position, SHADER_UNIFORM_VEC3);
        return *this;
    }

    tile_shader &tile_shader::set_ambient_light(const float r, const float g, const float b, const float a) {
        options.ambient_light[0] = r;
        options.ambient_light[1] = g;
        options.ambient_light[2] = b;
        options.ambient_light[3] = a;
        SetShaderValue(*shader, ambient_loc, options.ambient_light, SHADER_UNIFORM_VEC4);
        return *this;
    }

    tile_shader &tile_shader::set_ambient_light(const Color color) {
        return set_ambient_light(static_cast<float>(color.r) / 255.0f,
                                 static_cast<float>(color.g) / 255.0f,
                                 static_cast<float>(color.b) / 255.0f,
                                 static_cast<float>(color.a) / 255.0f);
    }

    tile_shader &tile_shader::set_ambient_light(const Vector4 color) {
        return set_ambient_light(color.x, color.y, color.z, color.w);
    }

    tile_shader &tile_shader::set_fog_color(const float r, const float g, const float b, const float a) {
        options.fog_color[0] = r;
        options.fog_color[1] = g;
        options.fog_color[2] = b;
        options.fog_color[3] = a;
        SetShaderValue(*shader, fog_color_loc, options.fog_color, SHADER_UNIFORM_VEC4);
        return *this;
    }

    tile_shader &tile_shader::set_fog_color(const Color color) {
        return set_fog_color(static_cast<float>(color.r) / 255.0f,
                             static_cast<float>(color.g) / 255.0f,
                             static_cast<float>(color.b) / 255.0f,
                             static_cast<float>(color.a) / 255.0f);
    }

    tile_shader &tile_shader::set_fog_color(const Vector4 color) {
        return set_fog_color(color.x, color.y, color.z, color.w);
    }

    tile_shader &tile_shader::set_fog_start(const float distance) {
        options.fog_start = distance;
        SetShaderValue(*shader, fog_start_loc, &options.fog_start, SHADER_UNIFORM_FLOAT);
        return *this;
    }

    tile_shader &tile_shader::set_fog_end(const float distance) {
        options.fog_end = distance;
        SetShaderValue(*shader, fog_end_loc, &options.fog_end, SHADER_UNIFORM_FLOAT);
        return *this;
    }

    tile_shader &tile_shader::set_skirt_drop(const float drop) {
        options.skirt_drop = drop;
        SetShaderValue(*shader, skirt_drop, &options.skirt_drop, SHADER_UNIFORM_FLOAT);
        return *this;
    }

    tile_shader &tile_shader::set_sun_direction(const Vector3 direction) {
        options.sun_direction[0] = direction.x;
        options.sun_direction[1] = direction.y;
        options.sun_direction[2] = direction.z;
        SetShaderValue(*shader, sun_dir_loc, options.sun_direction, SHADER_UNIFORM_VEC3);
        return *this;
    }

    tile_shader &tile_shader::set_sun_scale(const float scale) {
        options.sun_scale = scale;
        SetShaderValue(*shader, sun_scale_loc, &options.sun_scale, SHADER_UNIFORM_FLOAT);
        return *this;
    }

    tile_shader &tile_shader::set_height_scale(const float scale) {
        options.height_scale = scale;
        SetShaderValue(*shader, height_scale_loc, &options.height_scale, SHADER_UNIFORM_FLOAT);
        return *this;
    }

    tile_shader &tile_shader::set_normals_scale(const float scale) {
        options.normals_scale = scale;
        SetShaderValue(*shader, normal_scale_loc, &options.normals_scale, SHADER_UNIFORM_FLOAT);
        return *this;
    }
}
