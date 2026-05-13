#pragma once
#include <cmath>
#include <vector>

#include "raylib.h"
#include "raymath.h"
#include "tile.hpp"

namespace raytiles {
    using Meters = float;
    using MetersSq = double;
}

namespace raytiles::utils {
    /// Distance to the horizon from height h above a sphere of radius R:
    /// (R + h)^2 = R^2 + d^2
    /// d = sqrt(2Rh + h^2)
    /// d ≈ sqrt(2Rh) for h << R
    /// d ≈ 3.57 * sqrt(h)
    /// d ≈ horizon_ratio * sqrt(h)
    /// where R is Earth radius, h is height above Earth, and d is distance to horizon in Km.
    constexpr MetersSq horizon_ratio = 3.57f * 1000.0f; // convert to meters


    /// Calculates the distance to the horizon from a given
    /// height above the surface of the Earth.
    inline Meters distance_to_horizon(const Vector3 &position) {
        return horizon_ratio * std::sqrt(position.y);
    }

    /// Calculates the squared distance from a position to the center of a tile.
    /// Tile position is determined by its x and z indices and the tile size at the given zoom level.
    inline MetersSq distance_sq_to_tile(const Vector3 &position, const TileKey &tile, const float tile_size) {
        const float world_x = (static_cast<float>(tile.x) + 0.5f) * tile_size;
        const float world_z = (static_cast<float>(tile.z) + 0.5f) * tile_size;
        const float dx = position.x - world_x;
        const float dz = position.z - world_z;
        return dx * dx + dz * dz + position.y * position.y; // include height for better LOD selection
    }

    /// Calculates the distance from a position to the center of a tile.
    inline Meters distance_to_tile(const Vector3 &position, const TileKey &tile, const float tile_size) {
        return std::sqrt(distance_sq_to_tile(position, tile, tile_size));
    }


    /// Get height from image based on https://registry.opendata.aws/terrain-tiles/
    inline float get_height_from_image(const Image &img, int x, int y) {
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
}
