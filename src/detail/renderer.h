#pragma once

#include <vector>

#include "raylib.h"
#include "raii.hpp"
#include "tile.hpp"
#include "tile_shader.h"
#include "utils.hpp"

namespace raytiles {
    struct rendering_config;

    class renderer {
    public:
        explicit renderer(const rendering_config &conf);

        int draw(const Vector3 &position, const DebugView &draw_view);

        /// Draws a 2D HUD with streamer statistics (loaded / loading counts, etc.)
        /// and zoom labels above the tiles
        /// Call between `BeginDrawing` / `EndDrawing`, after `EndMode3D`.
        static void debug(const Camera3D &camera, const DebugView &draw_view);

        /// Draws 3D debug overlays (tile bounds). Call inside the same
        /// `BeginMode3D` / `EndMode3D` block as `draw`.
        static void debug_3d(const DebugView &draw_view);

        /// Sets the ambient light color sent to the displacement shader. Use this
        /// to drive day / night / weather lighting changes.
        void set_ambient_light(const Color color);

        /// Sets the ambient light color sent to the displacement shader. Use this
        /// to drive day / night / weather lighting changes.
        void set_ambient_light(const Vector4 color);

        /// Sets the ambient light color sent to the displacement shader. Use this
        /// to drive day / night / weather lighting changes.
        void set_ambient_light(const float r, const float g, const float b, const float a);

        /// Sets the fog color for distance attenuation. Match this to your sky
        /// color for a seamless horizon.
        void set_fog_color(const Color color);

        /// Sets the fog color for distance attenuation. Match this to your sky
        /// color for a seamless horizon.
        void set_fog_color(const Vector4 color);

        /// Sets the fog color for distance attenuation. Match this to your sky
        /// color for a seamless horizon.
        void set_fog_color(const float r, const float g, const float b, const float a);

        /// Sets the fog start distance — the distance from the camera at which
        /// colors begin to blend with the fog.
        void set_fog_start(const float distance);

        /// Sets the fog end distance — the distance from the camera at which
        /// colors are fully blended with the fog color.
        void set_fog_end(const float distance);

        /// Sets the heightmap scale factor, which exaggerates or flattens the
        /// terrain relief (drama factor).
        void set_height_scale(const float scale);

        /// Sets the normals scale factor to increase or reduce lighting contrast.
        void set_normals_scale(const float scale);

        /// Sets the sun direction vector used by the displacement shader's
        /// lighting calculations.
        void set_sun_direction(const Vector3 direction);

        /// Sets the sun lighting intensity, which controls the contrast between
        /// lit and shaded areas.
        void set_sun_scale(const float scale);

    private:
        struct DrawEntry {
            float dist_sq; // squared XZ distance from camera, used as sort key
            const tile_key *key; // non-owning, points into draw_view.rendering_tiles
            const loaded_tile *tile; // non-owning, same
            const tile_value *tv; // non-owning, points into draw_view.tiles
        };

        std::vector<DrawEntry> draw_order_{};

        tile_shader shader_;
        raii::material material{};
    };
}
