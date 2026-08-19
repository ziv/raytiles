#pragma once

#include <cstddef>
#include <cstdint>

#include "raytiles/raytiles.h" // Meters/Zoom aliases (also pulls raylib.h)
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

    /// Fully promoted tile — the *owner* record. Holds the RAII resources plus
    /// the tile's slot in the flat render list. Everything the draw loop needs
    /// lives in the matching `render_item`; this record exists for resource
    /// lifetime, `ground_height()` (CPU heightmap), and slot bookkeeping.
    struct loaded_tile {
        raii::texture tx_texture;
        raii::texture hm_texture;
        raii::image hm_image;
        raii::texture nl_texture;
        std::uint32_t slot;
    };

    /// One entry of the flat render list: everything needed to draw one tile,
    /// with zero further lookups. All raylib handles are non-owning copies of
    /// small PODs; ownership stays in the manager's resident map, and an item
    /// never outlives its owner (evicted together). Only valid within the
    /// frame it was obtained.
    struct render_item {
        Mesh mesh; // shared per-zoom mesh
        Texture2D albedo;
        Texture2D heightmap;
        Texture2D normals;
        Matrix transform; // user-space translate; m12/m14 double as the cull center
        float size; // world size, for the cull AABB
        double abs_x; // absolute tile center, kept in double for offset rebakes
        double abs_z;
        tile_key key; // backlink for eviction slot-fixups + debug labels
        bool visible; // frustum result, written in place each frame
        bool desired; // desired-set membership, for the debug overlay
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
