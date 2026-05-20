#pragma once
#include "detail/utils.hpp"

namespace raytiles {
    struct tile_shader_options {
        /// Distance (in meters) at which atmospheric fog starts to fade tiles to
        /// `fog_color`.
        Meters fog_start = 100000.0f;

        /// Distance (in meters) at which fog reaches full cover.
        Meters fog_end = 150000.0f;

        /// Vertical drop (in meters) of the skirt geometry below each tile's edge.
        /// Larger values hide cracks more reliably but cost more fill rate.
        /// Baked into shader. 0 disable this feature.
        Meters skirt_drop = 0.0f;

        /// Fog color (RGBA, 0..1). Match this to your sky color for a seamless
        /// horizon.
        float fog_color[4] = {0.0f, 0.0f, 1.0f, 1.0f};

        /// World ambient color (RGBA, 0..1). Drives day / night / weather
        /// lighting changes.
        float ambient_light[4] = {1.0f, 1.0f, 1.0f, 1.0f};

        /// Sun direction vector. The shader normalizes it internally; magnitude
        /// is irrelevant.
        float sun_direction[3] = {0.1f, 1.0f, 0.1f};

        /// Sun lighting intensity, controlling contrast between lit and shaded
        /// areas.
        float sun_scale = 1.0f;

        /// Scales the heightmap by this factor to exaggerate or flatten the
        /// terrain relief (drama factor).
        float height_scale = 1.0f;

        /// Scales the normals by this factor to increase or reduce lighting
        /// contrast. Higher values make the terrain look bumpier, but can cause
        /// lighting artifacts if the normals become too steep.
        float normals_scale = 1.0f;
    };

    class tile_shader {
    public:
        explicit tile_shader(const tile_shader_options &opts = {});

        /// Returns the underlying raylib `Shader` handle. Use this when you
        /// need to bind the shader to a `Material` or pass it to a raylib
        /// drawing call directly.
        const Shader &operator()() const noexcept { return *shader; }

        /// Pushes the current camera world-space position to the shader. Call
        /// this once per frame before drawing; the displacement shader uses it
        /// for fog falloff and per-vertex distance attenuation.
        tile_shader &set_camera_location(const Vector3 &position);

        /// Sets the world ambient light color (RGBA, 0..1) sent to the shader.
        /// Use this to drive day / night / weather lighting changes.
        tile_shader &set_ambient_light(float r, float g, float b, float a);

        /// Sets the world ambient light from a raylib `Color` (0..255 per
        /// channel); components are normalized to 0..1 before upload.
        tile_shader &set_ambient_light(Color color);

        /// Sets the world ambient light from a `Vector4` whose components are
        /// already in 0..1 range (x=r, y=g, z=b, w=a).
        tile_shader &set_ambient_light(Vector4 color);

        /// Sets the fog color (RGBA, 0..1) used for distance attenuation.
        /// Match this to your sky color for a seamless horizon.
        tile_shader &set_fog_color(float r, float g, float b, float a);

        /// Sets the fog color from a raylib `Color` (0..255 per channel);
        /// components are normalized to 0..1 before upload.
        tile_shader &set_fog_color(Color color);

        /// Sets the fog color from a `Vector4` whose components are already in
        /// 0..1 range (x=r, y=g, z=b, w=a).
        tile_shader &set_fog_color(Vector4 color);

        /// Sets the fog start distance (meters) — the distance from the camera
        /// at which colors begin to blend with the fog.
        tile_shader &set_fog_start(float distance);

        /// Sets the fog end distance (meters) — the distance from the camera
        /// at which colors are fully replaced by the fog color.
        tile_shader &set_fog_end(float distance);

        /// Sets the vertical drop (meters) of the skirt geometry below each
        /// tile's edge. Larger values hide cracks between LODs more reliably
        /// but cost more fill rate. Set to `0` to disable.
        tile_shader &set_skirt_drop(float drop);

        /// Sets the sun direction vector used by the shader's lighting pass.
        /// Magnitude is irrelevant — the shader normalizes it internally.
        tile_shader &set_sun_direction(Vector3 direction);

        /// Sets the sun lighting intensity. Controls the contrast between lit
        /// and shaded areas of the terrain.
        tile_shader &set_sun_scale(float scale);

        /// Sets the heightmap scale factor, exaggerating or flattening the
        /// terrain relief (drama factor). `1.0` keeps real-world elevation.
        tile_shader &set_height_scale(float scale);

        /// Sets the normals scale factor to increase or reduce lighting
        /// contrast. Higher values look bumpier but can produce lighting
        /// artifacts if the normals become too steep.
        tile_shader &set_normals_scale(float scale);
    private:
        // current option values, mirrored to the GPU on each setter call
        tile_shader_options options;

        // shaders slots locations
        int cam_pos_loc = -1;
        int ambient_loc = -1;
        int fog_color_loc = -1;
        int tex_albedo_loc = -1;
        int tex_height_loc = -1;
        int tex_normal_loc = -1;
        int sun_dir_loc = -1;
        int sun_scale_loc = -1;
        int height_scale_loc = -1;
        int normal_scale_loc = -1;
        int fog_start_loc = -1;
        int fog_end_loc = -1;
        int skirt_drop = -1;

        // shader
        raii::shader shader;
    };
}
