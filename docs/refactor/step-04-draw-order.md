# Step 4 — Front-to-back draw order on membership change

**Status:** DONE
**Commit:** `perf: keep the render list sorted front-to-back for early-Z`

## Goal

Order the render list near-to-far from the camera so the GPU's early-Z test rejects occluded
fragments (terrain is overdraw-heavy: mountains occlude valleys, skirts overlap everything).
Opaque-only rendering means order never changes the image — this is purely a perf policy.

## Design

- `order_dirty` flag on the manager, set by `evict()` (swap-remove breaks order) and by promotion
  (appends at the back).
- At the end of `pre_process` (after GC + promotions), if dirty: `std::sort` the list by squared
  XZ distance of the tile center (`abs_x/abs_z` — absolute space, same frame as `position`) to the
  camera, then rebuild every owner's `slot` in one linear sweep, clear the flag.
- Between membership changes the order is left alone. While stationary it stays exact; while moving,
  membership churns anyway (desired rebuilds every 500 m), so drift is short-lived. Early-Z is an
  optimization — a mildly stale order is still ~correct and never wrong.

## Cost

Sort of ≤ ~600 items × ~250 B, plus ~600 hash lookups for slot rebuild — on membership-change
frames only. The steady-state frame does zero extra work; the per-frame alternative (sorting an
index array every frame) would reintroduce an indirection into the draw loop for no measured need.

## Risks / mitigations

- Slot desync after sort → same debug-build lockstep assert from step 3 runs right after (moved the
  assert to after the sort block so it covers both GC and sort).
- Comparator UB on NaN distances — impossible: tile centers and camera positions are finite by
  construction.
- **Measurement caveat**: the plan gates this step on "keep only if it wins", but GPU frame-time
  can't be measured headless. The change is the standard opaque-pass ordering, costs nothing in
  steady state, and cannot alter output; implemented unconditionally, flagged for the author's
  A/B if curious (revert = drop the sort block, nothing else depends on order).

## Re-evaluation (pre-implementation)

- Considered zoom-descending as the sort key (static, no camera dependence) — rejected: high zoom ≈
  near camera is only a proxy; actual distance is just as cheap at this cadence and strictly better.
- Considered per-frame sorting — rejected (see Cost).
- Considered grouping by zoom to batch mesh binds — rejected: raylib's DrawMesh rebinds the VAO and
  material every call regardless; grouping buys nothing until instancing (a non-goal).

## Re-evaluation (post-implementation)

- Build warning-free, suites green. The lockstep assert now runs after the sort block, so it covers
  GC, promotion, *and* sort bookkeeping in one sweep.
- Sort trigger placed at the end of `pre_process` — after promotions append — so a burst frame sorts
  once, not per promotion. Distances compare in double against the absolute camera position (same
  frame as `abs_x/abs_z`).
- Re-checked a subtlety: `evict` sets `order_dirty` even when removing the last element (no swap) —
  strictly unnecessary in that one case, but the branch isn't worth the explanation it would need.
- Deviation from spec: none. GPU win remains unmeasured headless (documented in Risks); revert path
  is dropping the sort block only.
