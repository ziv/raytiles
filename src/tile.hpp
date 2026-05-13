#pragma once
#include <cstddef>

namespace raytiles {

    struct TileValue {
        float size;
        float threshold;
        raii::mesh mesh;
    };

    struct TileKey {
        int zoom;
        int x;
        int z;

        auto operator<=>(const TileKey &) const = default;
    };
}

template<>
struct std::hash<raytiles::TileKey> {
    std::size_t operator()(const raytiles::TileKey &key) const noexcept {
        std::size_t seed = 0;

        seed ^= key.zoom + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= key.x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= key.z + 0x9e3779b9 + (seed << 6) + (seed >> 2);

        return seed;
    }
};
