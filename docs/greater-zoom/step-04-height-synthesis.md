# Step 4 — Synchronous height synthesis above the native zoom

**Status:** DONE

## Spec

Worker heightmap path for `zoom > native_terrain_zoom` (plan §1.2):

1. Exact-tile cache hit → decode, done (unchanged).
2. Miss → fetch the native ancestor (`x >> dz, z >> dz`; cache-or-HTTP, cached as usual) →
   `decode_terrarium_floats` → walk the lineage `native+1 … zoom`, upsampling only the quadrant
   containing the target (`(x >> (zoom-level)) & 1`) → `encode_terrarium` → PNG-encode
   (`stbi_write_png_to_mem`, prototypes linked from raylib's rtextures — verified exported) →
   atomic cache write → serve in this job's payload → enqueue a background derive task (step 5).
3. No HTTP is attempted for heightmaps above the native zoom.

Normals above native (completes §1.6): disk cache honored (pre-baked allowed), otherwise default
flat normals in memory — no HTTP, no cache write.

## Tests (offline)

Seed only a z15 gradient heightmap (+ the tile's own z16/z17 texture) → z16 request yields a
payload whose height image is **byte-identical** to `encode(upsample(decode(parent)))` computed
independently in the test; the derived PNG lands in the cache; a z17 request works with only z15
seeded (chain); corrupt z15 → drop with reason.

## Re-evaluation

- Tests green: derived z16 payload is byte-identical to the independently computed
  decode→upsample→encode of the seeded ancestor; z17 chains through z16 with only z15 on disk;
  corrupt ancestor drops with a reason; above-native normals default with a dead host (no network).
- One discovery vs the plan: `stbi_write_png_to_mem` is *not* declared by stb_image_write.h's
  public prototype section (it sits in the implementation region, but with external linkage — the
  symbol is exported by raylib's rtextures, verified with `nm`). The prototype is therefore
  hand-declared in tile_source.cpp with a comment pinning the vendored signature (v1.16).
- The synchronous path upsamples only the target's quadrant chain — the requested tile is served
  after O(levels) 256²-float passes plus one PNG encode.
