#pragma once
#ifdef __EMSCRIPTEN__
#define GLSL_VERSION_HEADER "#version 300 es\nprecision mediump float;\n"
#else
#define GLSL_VERSION_HEADER "#version 330\n"
#endif

namespace raytiles::shaders {
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
    if (skirtDrop > 0.0)
    {
        float epsilon = 0.000001;
        bool isEdge = (vertexTexCoord.x < epsilon) ||
                      (vertexTexCoord.x > 1.0 - epsilon) ||
                      (vertexTexCoord.y < epsilon) ||
                      (vertexTexCoord.y > 1.0 - epsilon);
        if (isEdge)
        {
            displacedPosition.y -= skirtDrop * heightScale;
        }
    }

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
