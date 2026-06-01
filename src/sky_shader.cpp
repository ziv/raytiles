#include "detail/sky_shader.h"

#ifdef __EMSCRIPTEN__
#define GLSL_VERSION_HEADER "#version 300 es\nprecision mediump float;\n"
#else
#define GLSL_VERSION_HEADER "#version 330\n"
#endif

namespace raytiles {
    namespace {
        // language=GLSL
        constexpr auto vertex_shader = GLSL_VERSION_HEADER R"glsl(
// Input vertex attributes
in vec3 vertexPosition;

// Model-View-Projection matrix
uniform mat4 mvp;

// Output the local 3D position to the Fragment Shader instead of UVs
out vec3 fragLocalPos;

void main()
{
    // Pass the raw local position of the sphere's vertex
    fragLocalPos = vertexPosition;

    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)glsl";

        // language=GLSL
        constexpr auto fragment_shader = GLSL_VERSION_HEADER R"glsl(
#version 330

in vec3 fragLocalPos;

uniform vec3 zenithColor;
uniform vec3 horizonColor;
uniform float time;
// uniform vec3 cloudAmbient; // todo get the colors from options

out vec4 finalColor;

// noise primitives

vec2 hash2(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 43758.5453);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);

    float a = hash2(i).x;
    float b = hash2(i + vec2(1.0, 0.0)).x;
    float c = hash2(i + vec2(0.0, 1.0)).x;
    float d = hash2(i + vec2(1.0, 1.0)).x;

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// 5-octave FBM
float fbm(vec2 p) {
    float v = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int i = 0; i < 5; i++) {
        v += amp * valueNoise(p * freq);
        freq *= 2.1;
        amp  *= 0.5;
    }
    return v;
}

void main()
{
    // vec3 cloudAmbient = vec3(0.65, 0.65, 0.75);
    vec3 cloudAmbient = vec3(0.9, 0.9, 1.0);
    vec3 dir = normalize(fragLocalPos);

    float upFactor   = max(dir.y, 0.0);
    float blendFactor = clamp(pow(upFactor, 0.6), 0.0, 1.0);
    vec3 skyColor    = mix(horizonColor, zenithColor, blendFactor);

    // --- clouds ---
    // todo take cloudAltitude and threshold from the game to allow player controls the weather
    // Only draw clouds above the horizon
    if (dir.y > 0.0) {
        // Project the view direction onto a flat cloud plane at a fixed "altitude".
        // Dividing xz by y gives a perspective-correct plane intersection.
        float cloudAltitude = 0.45; // controls how high on the dome clouds appear
        vec2 cloudUV = dir.xz / (dir.y + cloudAltitude);

        // Slow horizontal drift
        cloudUV += vec2(time * 0.004, time * 0.001);

        float noise = fbm(cloudUV * 2.5);

        // Remap noise to a [0,1] cloud density, with a threshold to clear sky gaps
        float threshold = 0.48;
        float cloudDensity = smoothstep(threshold, threshold + 0.25, noise);

        // Fade clouds out near the horizon so there's no hard cut
        float horizonFade = smoothstep(0.0, 0.12, dir.y);
        cloudDensity *= horizonFade;

        vec3 cloudColor = mix(cloudAmbient, vec3(1.0), cloudDensity * 0.4);
        skyColor = mix(skyColor, cloudColor, cloudDensity * 0.85);
    }

    finalColor = vec4(skyColor, 1.0);
}
)glsl";
    }

    sky_shader::sky_shader(const sky_shader_options &opts)
        : options(opts),
          shader(raii::load_shader_from_memory(vertex_shader, fragment_shader)) {
        // cache slots
        zenith_color_loc = GetShaderLocation(*shader, "zenithColor");
        horizon_color_loc = GetShaderLocation(*shader, "horizonColor");
        time_loc = GetShaderLocation(*shader, "time");

        if (-1 == zenith_color_loc ||
            -1 == horizon_color_loc ||
            -1 == time_loc
        ) {
            throw std::runtime_error("Failed to get shader uniform locations");
        }

        SetShaderValue(*shader, zenith_color_loc, options.zenithColor, SHADER_UNIFORM_VEC4);
        SetShaderValue(*shader, horizon_color_loc, options.horizonColor, SHADER_UNIFORM_VEC4);
    }

    sky_shader &sky_shader::set_zenith_color(float r, float g, float b, float a) {
        options.zenithColor[0] = r;
        options.zenithColor[1] = g;
        options.zenithColor[2] = b;
        options.zenithColor[3] = a;
        SetShaderValue(*shader, zenith_color_loc, options.zenithColor, SHADER_UNIFORM_VEC4);
        return *this;
    }

    sky_shader &sky_shader::set_horizon_color(float r, float g, float b, float a) {
        options.horizonColor[0] = r;
        options.horizonColor[1] = g;
        options.horizonColor[2] = b;
        options.horizonColor[3] = a;
        SetShaderValue(*shader, horizon_color_loc, options.horizonColor, SHADER_UNIFORM_VEC4);
        return *this;
    }

    sky_shader &sky_shader::set_time(float time) {
        SetShaderValue(*shader, time_loc, &time, SHADER_UNIFORM_FLOAT);
        return *this;
    }
}
