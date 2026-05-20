#pragma once
#include "raytiles/detail/utils.hpp"

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
        explicit tile_shader(const tile_shader_options &options = {});

        void set_camera_location(const Vector3 &position);

    private:
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
