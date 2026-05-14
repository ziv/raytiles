#pragma once

#include <cstddef>
#include <future>
#include <string>

#include "raii.hpp"

namespace raytiles {
    struct tile_value {
        float size;
        float threshold;
        raii::mesh mesh;
    };

    struct tile_key {
        int zoom;
        int x;
        int z;

        auto operator<=>(const tile_key &) const = default;
    };

    /// In-flight download record. Holds the three shared_futures (texture,
    /// heightmap, normals) the worker pool resolves with raw file bytes, plus
    /// the precomputed world-space center of the tile.
    struct loading_tile {
        float tx;
        float tz;
        std::shared_future<std::string> tx_future;
        std::shared_future<std::string> hm_future;
        std::shared_future<std::string> nl_future;
    };

    /// Fully promoted tile: GPU textures uploaded, heightmap CPU image retained
    /// for `ground_height()` queries.
    struct loaded_tile {
        float tx;
        float tz;
        raii::texture tx_texture;
        raii::texture hm_texture;
        raii::image hm_image;
        raii::texture nl_texture;
    };
} // namespace raytiles

template<>
struct std::hash<raytiles::tile_key> {
    std::size_t operator()(const raytiles::tile_key &key) const noexcept {
        std::size_t seed = 0;

        seed ^= key.zoom + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= key.x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= key.z + 0x9e3779b9 + (seed << 6) + (seed >> 2);

        return seed;
    }
};
