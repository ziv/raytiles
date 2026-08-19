#include <span>

#include "raytiles/raytiles.h"
#include "detail/terrain_renderer.h"
#include "detail/raii.hpp"
#include "detail/tile_shader.h"

namespace raytiles {
    terrain_renderer::terrain_renderer(const rendering_config &conf) : shader_(conf) {
        material = raii::material{LoadMaterialDefault()};
        material->shader = shader_();
    }

    int terrain_renderer::draw(const Vector3 &camera_position, const std::span<const render_item> items) {
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

    void terrain_renderer::debug_3d(const std::span<const render_item> items) {
        for (const auto &item: items) {
            if (item.visible) {
                DrawCubeWires({item.transform.m12, 0.0f, item.transform.m14}, item.size, 1000.0f, item.size, GREEN);
            }
        }
    }

    void terrain_renderer::debug(const Camera3D &camera, const std::span<const render_item> items) {
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
}
