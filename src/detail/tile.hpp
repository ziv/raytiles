#pragma once

#include <cstddef>
#include <cstdint>
#include <future>
#include <string>

#include "raylib.h"
#include "raii.hpp"

namespace raytiles {
    struct tile_value {
        Meters size;
        MetersDSq threshold;
        raii::mesh mesh;
    };

    struct tile_key {
        int zoom;
        int x;
        int z;

        auto operator<=>(const tile_key &) const = default;
    };

    /// In-flight download record. Holds the three shared_futures (texture,
    /// heightmap, normals) the worker pool resolves with already-decoded
    /// raylib `Image` structs (pixels malloc'd by stb_image, owned by
    /// whoever consumes the future). The world-space center of the tile is
    /// also precomputed.
    ///
    /// Image is the raylib POD (data + w/h/mipmaps/format). The pool does
    /// NOT wrap it in raii::image so the consumer can decide whether to
    /// adopt it into a raii::image (heightmap path), upload-and-UnloadImage
    /// (texture / normals path), or UnloadImage on cancellation.
    struct loading_tile {
        float tx;
        float tz;
        std::shared_future<Image> tx_future;
        std::shared_future<Image> hm_future;
        std::shared_future<Image> nl_future;
    };

    /// Fully promoted tile: GPU textures uploaded, heightmap CPU image
    /// retained for `ground_height()` queries.
    struct loaded_tile {
        Meters size;
        float tx;
        float tz;
        raii::texture tx_texture;
        raii::texture hm_texture;
        raii::image hm_image;
        raii::texture nl_texture;
        bool in_frustum_this_frame;
    };
} // namespace raytiles

template<>
struct std::hash<raytiles::tile_key> {
    std::size_t operator()(const raytiles::tile_key &key) const noexcept {
        // Bit-pack into a 64-bit word, then apply SplitMix64's finalizer.
        // Field budget: zoom fits in 5 bits (max supported is 15); x/z fit in
        // ~30 bits each (at zoom 15 the world has 2^15 tiles per side, anchor
        // shifts keep us well inside int32). We cast through uint32_t first so
        // negative coordinates (anchor-relative) preserve their bit pattern.
        const auto z32 = static_cast<std::uint64_t>(static_cast<std::uint32_t>(key.zoom));
        const auto x32 = static_cast<std::uint64_t>(static_cast<std::uint32_t>(key.x));
        const auto y32 = static_cast<std::uint64_t>(static_cast<std::uint32_t>(key.z));

        std::uint64_t h = z32 << 60 ^ x32 << 30 ^ y32;

        // SplitMix64 finalizer — strong avalanche, no loops.
        h ^= h >> 30;
        h *= 0xbf58476d1ce4e5b9ULL;
        h ^= h >> 27;
        h *= 0x94d049bb133111ebULL;
        h ^= h >> 31;

        return h;
    }
};
