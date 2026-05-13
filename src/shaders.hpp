#pragma once
#ifdef __EMSCRIPTEN__
#define GLSL_VERSION_HEADER "#version 300 es\nprecision mediump float;\n"
#else
#define GLSL_VERSION_HEADER "#version 330\n"
#endif

namespace raytiles::shaders {
    constexpr auto vertex_shader = GLSL_VERSION_HEADER R"(
in vec3 vertexPosition;         // current vertex position
in vec2 vertexTexCoord;         // current vertex uv

uniform mat4 mvp;               // model view projection
uniform mat4 matModel;          // position matrix
uniform sampler2D heightMap;    // heightmap input
uniform float heightScale;      // scale factor input
uniform float normalScale;      // scale factor input
uniform vec3 cameraPosition;    // camera world position (for distance -> fog)

out vec2 fragTexCoord;          // uv for the fragment
out float fragCamDist;          // distance from the camera to the vertex, for fog calculation in the fragment shader

void main()
{
    // moving the value to the fragment
    fragTexCoord = vertexTexCoord;

    // terrarium heightmap calculations
    // sample the heightmap; the RGB values are combined into a single height value.
    vec3 color = texture(heightMap, vertexTexCoord).rgb;
    vec3 c = color * 255.0;
    float heightValue = (c.r * 256.0 + c.g + c.b / 256.0) - 32768.0;

    // update the displacement
    vec3 displacedPosition = vertexPosition;
    displacedPosition.y += heightValue * heightScale;

    vec3 worldPosition = vec3(matModel * vec4(displacedPosition, 1.0));
    fragCamDist = distance(worldPosition, cameraPosition);

    // real pixel position on screen
    gl_Position = mvp * vec4(displacedPosition, 1.0);
}
)";

    constexpr auto fragment_shader = GLSL_VERSION_HEADER R"(
in vec2 fragTexCoord;
in float fragCamDist;

uniform sampler2D texture0;
uniform sampler2D normalMap;
uniform vec4 ambientLight;
uniform vec4 fogColor;
uniform float fogStart;
uniform float fogEnd;
uniform vec3 sunDir;
uniform float normalScale;
uniform float sunScale;

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
)";
}
