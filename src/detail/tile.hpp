#pragma once

#include <cstddef>
#include <cstdint>

#include "raylib.h"
#include "raytiles/raytiles.h" // Meters / MetersDSq aliases
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

    /// A fully downloaded-and-decoded tile, produced entirely on a worker
    /// thread and handed to the main thread via `tile_source::drain()`.
    ///
    /// The three images are move-only single owners of their stb-allocated
    /// pixel buffers (`raii::image` frees via `UnloadImage`, which is a plain
    /// `RL_FREE(data)` — safe on any thread, no GL involved). The payload is
    /// moved worker -> ready queue -> promotion; pixels are never copied.
    struct tile_payload {
        tile_key key;
        raii::image albedo;
        raii::image height;
        raii::image normals;
    };

    /// Fully promoted tile: GPU textures uploaded, heightmap CPU image
    /// retained for `ground_height()` queries.
    struct loaded_tile {
        Meters size;
        double tx;
        double tz;
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
