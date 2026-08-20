# Step 2 — Per-zoom array, `get_loading` fix, dead-code removal

**Status:** DONE
**Commit:** `fix: correct loading progress and replace per-zoom map with array`

## Goal

Three small, independent cleanups the plan batches together (plan §3, step 2). No architecture
change; behavior changes only where the behavior was a bug (`get_loading`).

## Design

1. **`tiles` map → array.** `unordered_map<Zoom, tile_value>` → `std::array<tile_value, zoom_levels>`
   indexed `zoom - base_zoom` (resolves the `// todo replace with array (perf)` in utils.hpp).
   Private helper `zoom_value(Zoom)` centralizes the index math. `data_view.tiles` becomes a const
   ref to the array + a `base_zoom` field so the renderer indexes instead of hashing
   (`tiles.find(key.zoom)` → `tiles[key.zoom - base_zoom]`). Unused trailing slots hold
   default-constructed `tile_value` (empty `raii::mesh`, never dereferenced — nothing ever produces
   a key outside `[base_zoom, max_zoom]`, invariant-tested in step 1).
2. **`get_loading` fix.** Current formula `1 - loading_count/desired_count` conflates "in flight"
   with "desired" (which includes already-resident tiles): it reports 0.0 both before anything is
   requested *and* when everything is loaded, and jumps when the desired set rebuilds. New
   definition: **fraction of the desired set that is resident** — `desired ∩ rendering / desired`,
   0 when the desired set is empty. Monotonic during initial load, 1.0 at completion. O(desired)
   per call; it's called once per frame by loading screens only.
3. **Dead code / typos.** Delete unused `draw_entry` (tiles_renderer.h) and the declared-but-never-
   defined `pool::log_line` (downloader.h). Fix comment typos ("evry", "nad", "anchore"), drop the
   informational anchor TraceLog from LOG_WARNING to LOG_INFO, remove stale commented-out lines in
   `ground_height`.

## Risks / mitigations

- Array indexing with an out-of-range zoom would be UB where the map would insert/throw. All key
  producers (lod, spawn subdivision) are bounded by construction and step-1 invariant tests; the
  helper keeps the index math in one place for step 6 to add an assert if wanted.
- `get_loading` semantic change is user-visible (quick_start progress display) — it's the plan's
  documented intent and strictly more truthful; `is_loading()` gating is untouched.

## Re-evaluation (pre-implementation)

- Considered `std::vector<tile_value>` sized `max_zoom - base_zoom + 1` — rejected: fixed-capacity
  `std::array` matches the existing `thresholds`/`skirt_overlap` convention and avoids a heap
  allocation; 7 slots × ~56 bytes of possible waste is irrelevant.
- Considered fixing `is_loading()`'s "first empty = done" quirk here — deferred to step 6
  (tile_store) where loading-state bookkeeping gets restructured anyway; batching it here would
  blur this step's "mechanical" character.
- `data_view` changes are throwaway (step 3 deletes the struct) but required for compilation;
  kept minimal on purpose.

## Re-evaluation (post-implementation)

- All 9 test cases / 25 097 assertions still green; zero warnings from project code (the one
  build warning is inside raylib's vendored stb_vorbis).
- The renderer's per-tile hash lookup is gone (`tiles.find(key.zoom)` → array index); `ground_height`,
  `spawn`, eviction, and promotion now index through `zoom_value()`.
- Kept the `IsImageValid` guard in `ground_height` (was marked "remove?") — a stale image would fail
  *silently* into garbage heights; the guard is one branch on a cold path. Comment updated to say why.
- `get_loading` now returns resident/desired; grep-verified that every call site (quick_start,
  sandbox utils/main/base_line/spline loading loops, C wrapper passthrough) gates on `is_loading()`
  and uses the value for display only — no logic depends on the old formula.
- Deviation from spec: none.
