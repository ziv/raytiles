#pragma once
#include <cmath>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "raylib.h"
#include "raymath.h"
#include "tile.hpp"

namespace raytiles {
    constexpr float max_world_height = 8848.0f; // Mount Everest height in meters
    constexpr int min_resolution = 4;
    constexpr int max_resolution = 256;

    struct data_view {
        Frustum &frustum;
        std::unordered_map<tile_key, loaded_tile> &rendering_tiles;
        std::unordered_map<Zoom, tile_value> &tiles; // todo replace with array (perf)
        std::unordered_set<tile_key> &desired_keys;
    };
}

namespace raytiles::utils {
    /// Distance to the horizon from height h above a sphere of radius R:
    /// (R + h)^2 = R^2 + d^2
    /// d = sqrt(2Rh + h^2)
    /// d ≈ sqrt(2Rh) for h << R
    /// d ≈ 3.57 * sqrt(h)
    /// d ≈ horizon_ratio * sqrt(h)
    /// where R is Earth radius, h is height above Earth, and d is distance to horizon in Km.
    constexpr MetersDSq horizon_ratio = 3.57f * 1000.0f; // convert to meters

    /// Calculates the squared distance from a position to the center of a tile.
    /// Tile position is determined by its x and z indices and the tile size at the given zoom level.
    inline MetersDSq distance_sq_to_tile(const Vector3 &position, const tile_key &tile, const float tile_size) {
        const float world_x = (static_cast<float>(tile.x) + 0.5f) * tile_size;
        const float world_z = (static_cast<float>(tile.z) + 0.5f) * tile_size;
        const float dx = position.x - world_x;
        const float dz = position.z - world_z;
        return static_cast<double>(dx) * static_cast<double>(dx) +
               static_cast<double>(dz) * static_cast<double>(dz) +
               static_cast<double>(position.y) * static_cast<double>(position.y); // include height for better LOD selection
    }

    /// Calculate the distance from a position to center of a tile on XZ plane
    inline MetersDSq distance_sq_to_tile_xz(const Vector3 &position, const tile_key &tile, const float tile_size) {
        const float world_x = (static_cast<float>(tile.x) + 0.5f) * tile_size;
        const float world_z = (static_cast<float>(tile.z) + 0.5f) * tile_size;
        const float dx = position.x - world_x;
        const float dz = position.z - world_z;
        return static_cast<double>(dx) * static_cast<double>(dx) +
               static_cast<double>(dz) * static_cast<double>(dz);
    }

    /// Calculates the distance from a position to the center of a tile.
    inline Meters distance_to_tile(const Vector3 &position, const tile_key &tile, const float tile_size) {
        return std::sqrt(static_cast<float>(distance_sq_to_tile(position, tile, tile_size)));
    }

    inline MetersDSq calculate_horizon(const Vector3 &position) {
        const auto d = horizon_ratio * std::max(position.y, 1.0f);
        return d * d;
    }

    /// Get height from image based on https://registry.opendata.aws/terrain-tiles/
    inline Meters get_height_from_image(const Image &img, int x, int y) {
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= img.width) x = img.width - 1;
        if (y >= img.height) y = img.height - 1;

        const auto pixels = static_cast<const unsigned char *>(img.data);
        unsigned char r, g, b;

        /// a better performance function instead of using
            /// raylib's GetImageColor(img, px, pz)
        if (img.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8) {
            const int index = (y * img.width + x) * 3;
            r = pixels[index];
            g = pixels[index + 1];
            b = pixels[index + 2];
        } else if (img.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
            const int index = (y * img.width + x) * 4;
            r = pixels[index];
            g = pixels[index + 1];
            b = pixels[index + 2];
        } else {
            return 0.0f;
        }

        return static_cast<float>(r) * 256.0f + static_cast<float>(g) + static_cast<float>(b) / 256.0f - 32768.0f;
    }

    inline void normalize_plane(Plane &p) {
        if (const float mag = sqrtf(p.normal.x * p.normal.x + p.normal.y * p.normal.y + p.normal.z * p.normal.z); mag > 0.0f) {
            p.normal.x /= mag;
            p.normal.y /= mag;
            p.normal.z /= mag;
            p.distance /= mag;
        }
    }

    inline Frustum extract_frustum(const Camera3D &camera, const float near_plane, const float far_plane) {
        const float aspect = static_cast<float>(GetScreenWidth()) / static_cast<float>(GetScreenHeight());
        const Matrix view = GetCameraMatrix(camera);
        const Matrix proj = MatrixPerspective(camera.fovy * DEG2RAD, aspect, near_plane, far_plane);

        auto [m0, m4, m8, m12, m1, m5, m9, m13, m2, m6, m10, m14, m3, m7, m11, m15] = MatrixMultiply(view, proj);

        Frustum frustum{};
        // left
        frustum.planes[0].normal.x = m3 + m0;
        frustum.planes[0].normal.y = m7 + m4;
        frustum.planes[0].normal.z = m11 + m8;
        frustum.planes[0].distance = m15 + m12;
        // right
        frustum.planes[1].normal.x = m3 - m0;
        frustum.planes[1].normal.y = m7 - m4;
        frustum.planes[1].normal.z = m11 - m8;
        frustum.planes[1].distance = m15 - m12;
        // bottom
        frustum.planes[2].normal.x = m3 + m1;
        frustum.planes[2].normal.y = m7 + m5;
        frustum.planes[2].normal.z = m11 + m9;
        frustum.planes[2].distance = m15 + m13;
        // top
        frustum.planes[3].normal.x = m3 - m1;
        frustum.planes[3].normal.y = m7 - m5;
        frustum.planes[3].normal.z = m11 - m9;
        frustum.planes[3].distance = m15 - m13;
        // near
        frustum.planes[4].normal.x = m3 + m2;
        frustum.planes[4].normal.y = m7 + m6;
        frustum.planes[4].normal.z = m11 + m10;
        frustum.planes[4].distance = m15 + m14;
        // far
        frustum.planes[5].normal.x = m3 - m2;
        frustum.planes[5].normal.y = m7 - m6;
        frustum.planes[5].normal.z = m11 - m10;
        frustum.planes[5].distance = m15 - m14;

        // normalize all
        for (auto &plane: frustum.planes) normalize_plane(plane);
        return frustum;
    }

    inline bool is_tile_in_frustum(const float x, const float z, const float size, const Frustum &frustum) {
        const auto s = size / 2.0f;
        // AABB corners
        const Vector3 min = {x - s, 0.0f, z - s};
        const Vector3 max = {x + s, max_world_height, z + s};

        for (const auto [normal, distance]: frustum.planes) {
            Vector3 p;
            p.x = normal.x > 0 ? max.x : min.x;
            p.y = normal.y > 0 ? max.y : min.y;
            p.z = normal.z > 0 ? max.z : min.z;

            const float d = normal.x * p.x +
                            normal.y * p.y +
                            normal.z * p.z +
                            distance;

            if (d < 0) return false;
        }
        return true; // should render
    }
}
