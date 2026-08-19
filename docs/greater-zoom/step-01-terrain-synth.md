# Step 1 — Pure synthesis helpers

**Status:** DONE

## Spec

New header `src/detail/terrain_synth.hpp`, namespace `raytiles::synth` — pure (no raylib calls,
no I/O, worker-safe), mirroring `lod.hpp`'s testability contract:

- `decode_terrarium_floats(const Image&) → std::vector<float>` — RGB(A)8 Terrarium → meters.
- `upsample_quadrant(std::span<const float>, w, h, qx, qz) → std::vector<float>` — one quadrant
  (`qx, qz ∈ {0,1}`) of a `w×h` grid upsampled 2× to `w×h`, texel-center-aligned bilinear
  (`src = q·w/2 + (i+0.5)/2 − 0.5`), edges clamped to the source.
- `encode_terrarium(std::span<const float>, w, h) → Image` — heights → Terrarium RGB8. Encoding is
  carry-safe 24-bit fixed point: `fixed = clamp(llround((h+32768)·256), 0, 0xFFFFFF)`, split into
  r/g/b bytes — never per-channel rounding (that spikes at `g` wrap boundaries). malloc'd pixels,
  `UnloadImage`-compatible.
- `default_normals_image(size) → Image` — solid `RGB(128,128,255)` = flat up-normal.

## Tests (tests/terrain_synth_tests.cpp)

Round-trip exactness at 1/256 m granularity incl. carry boundaries (255+255/256 m); decode of
encode equals input within quantization; upsample of a linear ramp is exact in the interior and
clamped at edges; sibling shared-edge continuity (q0's last column and q1's first column are the
two adjacent fine samples straddling the parent boundary — half a source step apart on a ramp);
flat-normals pixel/format check.

## Re-evaluation

- 6 test cases green: round-trip at 1/256 m, carry-boundary encoding, ramp exactness, sibling
  shared-edge continuity, vertical quadrant symmetry, flat-normals check.
- Deviation from plan: none. `upsample_normals_quadrant` was never written — decision 1.6 removed
  normals derivation before implementation started.
