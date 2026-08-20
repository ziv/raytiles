# Step 1 — Extract pure `lod` policy + real test suite

**Status:** DONE
**Commit:** `refactor: extract pure lod policy and add unit test suite`

## Goal

Move the desired-set computation (`tiles_manager::process_current_location` scan loop +
`build_required` recursion + the horizon cap) into a pure, stateless function in
`src/detail/lod.hpp`, and build the regression harness every later step leans on.
**Zero behavior change** — proven by an in-test reference implementation, not by eye.

## Design

New header `src/detail/lod.hpp` (header-only, no raylib *calls*, no I/O, no state; raylib **types**
like `Vector3` are fine):

```cpp
namespace raytiles::lod {
struct options {
    int   base_zoom;
    int   max_zoom;
    float base_tile_size;      // meters, at base_zoom
    int   rendering_radius;    // in base-zoom tiles
    std::array<float, zoom_levels> thresholds;  // plain meters, as in the public config
};
// Appends the desired keys for `position` (absolute space) into `out`.
// Deterministic in (opts, position). Does not clear `out`.
void desired_tiles(const options& opts, const Vector3& position, std::vector<tile_key>& out);
}
```

Decisions:
- **Derived values computed inside the call** (per-zoom size `base/2^i`, squared thresholds,
  horizon radius). 7 multiplies once per ~500 m movement — not worth caching state for; statelessness
  is what makes the function trivially testable.
- **Appends into a caller-owned vector** so `tiles_manager` can reuse a scratch buffer
  (steady-state: zero allocations once capacity is warm).
- The algorithm is a *verbatim move*: outer disc scan with `allowed_radius = (r-1)²`, recursion with
  the same order of checks (max-zoom short-circuit → horizon reject → threshold accept → subdivide),
  reusing `utils::distance_sq_to_tile` / `utils::calculate_horizon` (lod.hpp includes utils.hpp;
  the raylib-calling inlines there are never instantiated by lod or the tests, so tests still link
  without raylib). Bug-compatible on purpose — including the y²-in-distance quirk and the
  dimensionally odd horizon formula; changing policy is not this step's job.

`tiles_manager` changes:
- ctor builds a `lod::options` member from `tiles_manager_options`.
- `process_current_location` becomes: clear scratch vector → `lod::desired_tiles` → rebuild
  `desired_keys` set from it → spawn missing (unchanged loop).
- `build_required` and its decl are deleted.

## Tests (the real point of this step)

`run_tests` gets include paths for `src/`, `include/`, and raylib's *headers* (interface include
dirs only — the test binary still links no raylib, same trick as bindings mode).

`tests/lod_tests.cpp`:
1. **Equivalence vs reference**: the *current* algorithm, copied verbatim into the test file as
   `reference_desired()` (with its own per-zoom size/threshold² table exactly as the manager ctor
   builds it), compared as sorted sets against `lod::desired_tiles` over a grid of positions
   × altitudes (center/edge-of-tile positions; y ∈ {2, 500, 5 000, 60 000}; a few off-origin and
   negative-coordinate points). This is the actual proof the move changed nothing.
2. **Structural invariants** (on default options across the same grid): no key together with its
   ancestor; all keys within zoom bounds; base-zoom keys inside the scan disc; result
   is duplicate-free.
3. **Exact snapshots**: 3 fixed positions → total count, per-zoom counts, and membership of a few
   hand-picked keys. Values derived once from the reference implementation (procedure: run with a
   temporary printer, bake numbers in, delete printer).

`tests/utils_tests.cpp`:
- Terrarium decode: synthetic RGB and RGBA images with known-encoded heights (0 m, 8848 m,
  −415 m, fractional) round-trip through `get_height_from_image`; border clamping (negative /
  ≥ width indices); unsupported format returns 0.
- `tile_key`: hash is deterministic, equal keys hash equal, all keys in a 32×32×7 grid are
  collision-free at 64 bits (sanity, not a distribution proof).

Delete `tests/example_tests.cpp`. CMake: `run_tests` sources = tests_main + lod_tests + utils_tests;
add `src/detail/lod.hpp` to the library source list.

## Risks / mitigations

- *Reference impl drifts from what shipped* → it is copied from the exact HEAD revision this step
  starts from (`141da96` + none of my changes), noted in the test file header.
- *utils.hpp pulls raylib link deps into tests* → only if a test instantiates `extract_frustum`
  et al.; tests don't. Verified by linking run_tests without raylib.
- *Float nondeterminism across platforms breaks snapshots* → snapshot positions chosen away from
  threshold boundaries (no distance within 1 % of a threshold); equivalence test compares two
  in-process computations so it's platform-independent by construction.

## Re-evaluation (pre-implementation)

- Considered making `lod::options` hold precomputed squared thresholds — rejected: two
  representations of the same config invite drift; the public array is meters, keep it.
- Considered folding `desired_keys`-set ownership into lod — rejected: policy returns *what*, the
  store decides *how to index it* (set today, maybe sorted vec later).
- Considered testing via the old private method with friend declarations — rejected: friends couple
  tests to the class; extraction is the point.

## Re-evaluation (post-implementation)

- **Equivalence proven**: `lod::desired_tiles` output identical (size + full membership) to the
  verbatim reference copy across 28 probe positions × default options **and** × a non-default
  options set (base 11 / max 14, radius 4) — 56 configurations, all green. 25 097 assertions total
  in the suite.
- **Invariants hold** on every probe: duplicate-free, zoom-bounded, no ancestor/descendant pairs,
  every key descends from a base tile inside the scan disc.
- **Snapshots baked** (derived via the one-shot printer, cross-checked by the equivalence suite):
  origin @500 m → 252 keys (64 at z15, 0 at z9); mid-tile @5 km → 252 keys (36 at z9, 0 at z15 —
  the y² distance term keeps max zoom out); origin @60 km → 117 keys, refinement capped at z11.
  The last two pin the altitude-in-distance behavior explicitly, so any future change to that
  policy (it is arguably a quirk) will be a *conscious* snapshot update.
- **`run_tests` links without raylib** as designed — raylib enters the test target as headers only
  (`INTERFACE_INCLUDE_DIRECTORIES`), same trick as bindings mode.
- Library + all sandbox targets build with zero warnings under `-Wall -Wextra -Wpedantic`.
- One transient hiccup: first reconfigure re-cloned raylib (FetchContent cache had been pruned) and
  failed mid-clone; the retry configured clean. Nothing to do.
- Deviations from spec: none. Duplicate-freeness turned out to be guaranteed structurally (each key
  is reachable from exactly one root/subdivision path), so the vector needs no dedup — asserted in
  tests rather than assumed.
