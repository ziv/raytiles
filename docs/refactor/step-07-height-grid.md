# Step 7 — Height grid: `uint16` samples + bilinear `ground_height`

**Status:** DONE
**Commit:** `perf: replace retained heightmap images with uint16 height grids`

## Goal

Stop retaining the full decoded RGB(A) heightmap `Image` (192–256 KB/tile) whose only consumer is
`ground_height()`. Replace with a typed grid: `struct height_grid { int width, height;
vector<uint16_t> samples; }` — 128 KB/tile (~33–50 % of the height-query RAM back; ~40 MB at 600
residents) — and upgrade the query from nearest-neighbor to bilinear.

## Design

- **Encoding**: `sample = round(height_m) + 32768` — integer-meter resolution, ±0.5 m worst case.
  Justified: Mapzen terrain vertical accuracy is meters; the consumer is collision/AGL logic; the
  GPU displacement path still decodes full Terrarium precision from the texture, so visuals are
  untouched. (Float samples were rejected: 256 KB/tile is *worse* than the image; sub-meter
  fixed-point tricks don't fit the ±11 km range in 16 bits.)
- **Built on the worker**, not at promote time: `tile_payload` gains a `height_grid heights` built
  right after the heightmap decode (pure CPU math — no raylib, thread-safe). A 65 k-texel
  conversion (~0.1–0.2 ms) per tile would otherwise eat a visible chunk of the 2 ms/frame upload
  budget at 8 uploads/frame.
- **Pure helpers in utils.hpp** (unit-testable like everything else there):
  `build_height_grid(const Image&)` and `sample_height_grid(const height_grid&, u, v)` — bilinear
  with texel centers at `(i + 0.5) / n` and edge clamping.
- `resident_tile.hm_image` → `height_grid heights`; the heightmap image is now freed right after
  texture upload (it lives only for the ride to the GPU). `ground_height` samples the grid;
  the `IsImageValid` guard becomes an empty-grid guard.

## Behavior changes (intended)

- `ground_height` resolution: full Terrarium fraction → integer meters, **but** bilinear across
  texels (was nearest) — smoother and more physically sensible for a moving camera; the old
  stair-stepping at zoom-9 (~260 m/texel) disappears.

## Tests

Extend `tests/utils_tests.cpp`:
- `build_height_grid` from a synthetic Terrarium image → samples equal `round(h) + 32768`.
- `sample_height_grid` at texel centers returns the stored heights; the midpoint between two texels
  returns their average; `u/v` at 0 and 1 clamp to the border texels (no out-of-range reads).

## Risks / mitigations

- ±0.5 m quantization: documented on the public `ground_height` doc comment in step 8; if a future
  consumer needs sub-meter, the grid encoding is one function to change.
- Payloads temporarily carry image + grid (+128 KB) while queued — bounded by the upload queue
  length, transient.

## Re-evaluation (pre-implementation)

- Considered building the grid at promote time to keep `tile_source` "dumb" — rejected: it's the
  terrain payload's natural shape (the source already decodes PNGs; deriving the query grid is the
  same category of work), and worker time is free parallelism while main-thread budget is the
  scarcest resource in the design.
- Considered keeping nearest-neighbor to stay bit-compatible — rejected: the plan calls for
  bilinear, the old behavior was an artifact, and the snapshot-style equivalence that protected the
  lod extraction has no analog here (the function is newly-specified, tested directly).

## Re-evaluation (post-implementation)

- Build warning-free; 17 test cases / 25 143 assertions green, including the new grid suites
  (encode, texel-center identity, midpoint average, edge clamping, vertical interpolation).
- Grid is built on the worker *after* the last cancel check and *before* taking the queue lock —
  no lock is held during the 65 k-texel conversion.
- The promote loop no longer moves an `raii::image` into the resident record at all: all three
  payload images die at the end of the iteration, right after their GPU upload.
- The `ground_height` walk itself is unchanged (max→base zoom, first resident tile wins) — only the
  sampling under it changed, as specced.
- Deviation from spec: none.
- Author visual checkpoint (deferred): fly low over relief — ground collision in the demo should
  feel identical or smoother (no 260 m stair-stepping at coarse zooms).
