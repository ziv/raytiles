# Step 6 — `tiles_manager` → `tile_store`: intent-named frame steps + gated coverage GC

**Status:** DONE
**Commit:** `refactor: rename tiles_manager to tile_store with intent-named frame steps`

## Goal

Finish the composition picture (plan §2.4): the resident-set component gets its real name and frame
steps named for *what they do*, not *when they run*; the one remaining per-frame waste (coverage
checks against an unchanged world) gets gated.

## Design

Renames (mechanical, no behavior change):

| old | new |
|---|---|
| `tiles_manager` / `tiles_manager_options` | `tile_store` / `tile_store_options` |
| `src/detail/tiles_manager.h`, `src/tiles_manager.cpp` | `src/detail/tile_store.h`, `src/tile_store.cpp` |
| `pre_process(pos, offset)` | split: `reconcile(pos)` (GC) + `promote(offset)` (drain → budgeted upload → sort) |
| `process(pos)` / `process_current_location` | `update_desired(pos)` |
| `post_process(frustum, offset)` | `cull(frustum, offset)` |
| `rendering_tiles` / `loaded_tile` | `resident_tiles` / `resident_tile` |
| member `tile_manager` in streamer | `store` |

Frame order in `streamer::update` is unchanged: reconcile → promote → (moved?) update_desired →
cull. The loading-finished flip moves from cull (arbitrary placement) into the end of `promote`
(where the queues it inspects live); `is_loading()` is only observed after `update()` returns, so
the visible behavior is identical.

**Gated coverage GC.** Today `is_tile_covered` (up to 21 map lookups per candidate) runs every frame
for every non-desired-but-visible-and-in-area tile. Coverage can only *gain* a cover through
promotion, and the candidate set only changes with the desired set — so a `coverage_dirty` flag set
by promote (≥1 promotion) and by update_desired (set rebuilt), cleared after the GC pass, is
*exact*, not a heuristic: evictions only remove cover, which can only flip the answer toward "keep"
(fail-safe). Same-pass ordering effects are identical to today's erase_if semantics.

## Risks / mitigations

- Rename fallout across the public header (`raytiles.h` forward-declares the class and holds a
  `unique_ptr` member) — private members only, ABI/API-neutral for consumers; verified by compiling
  all sandbox apps + C wrapper unchanged.
- The coverage gate keeps a should-be-evicted tile alive until the next promotion/desired change —
  by construction those are the only events that could have made it evictable, so the delay is zero
  frames. Argued in the doc rather than tested (needs GL for a live-tile test).

## Re-evaluation (pre-implementation)

- Considered also renaming `tile_value` → `zoom_level` (plan's sketch name) — rejected: it collides
  conceptually with the `zoom_value()` accessor; revisit in step 8 if the name still grates.
- Considered folding the sort into `cull` (it consumes `position` too) — rejected: the sort belongs
  with the events that disturb order (promote/evict), and promote is the last of those in the frame.
- Considered gating on `order_dirty` instead of a new flag — rejected: order_dirty is cleared inside
  promote before reconcile runs next frame; a dedicated flag with promote/update_desired producers
  and reconcile as the consumer is the honest dependency graph.

## Re-evaluation (post-implementation)

- Build warning-free, all 15 test cases green.
- One signature deviation from the spec table: the sort origin lives in `promote`, so it takes
  `(position, world_offset)`, not just the offset — discovered when moving the sort block; the
  header doc explains both parameters.
- `streamer::update` now reads as the intended orchestration: `reconcile → promote →
  (moved? update_desired) → cull`, each name saying what it does.
- **Errata (found by the author running the demo, fixed after close-out):** moving the
  loading-finished flip from `post_process` into `promote` was *not* behavior-identical as claimed
  in the Design section — `promote` runs before the first `update_desired`, so the flag flipped on
  frame one with nothing yet requested and the loading screen never showed. Fixed by guarding the
  flip on `!desired_keys.empty()` ("a desired set exists and is fully serviced"). Lesson recorded:
  "X is only observed after update() returns" does not make intra-frame reordering safe when the
  state X reads is written *later in the same frame*.
- Coverage gate wired exactly as argued: producers are promote (`coverage_dirty` on new resident)
  and update_desired (candidate set changed); reconcile consumes and clears. Eviction deliberately
  does *not* set it (removals only un-cover, which biases toward "keep" — fail-safe).
- The `lod_tests.cpp` header comment still names `tiles_manager::process_current_location` — left
  intentionally: it documents where the reference algorithm was copied *from* at commit `141da96`,
  which is a historical fact.
- Deviations otherwise: none.
