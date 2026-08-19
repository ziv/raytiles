# Step 3 — The render list: one flat structure with everything needed to draw

**Status:** DONE (pending author's visual checkpoint, see below)
**Commit:** `refactor: flat render list; renderer consumes a span instead of manager maps`

## Goal

Introduce the plan's central data structure (plan §2.1): a contiguous `std::vector<render_item>`
holding, per resident tile, everything the draw loop needs — mesh handle, three texture handles,
baked user-space transform, cull data, flags. The renderer's input becomes
`std::span<const render_item>`; `data_view` (the renderer borrowing three manager maps) is deleted.

## Design

### `render_item` (src/detail/tile.hpp)

```cpp
struct render_item {
    Mesh      mesh;        // non-owning copies of raylib PODs; ownership stays
    Texture2D albedo;      // in the manager's resident map (raii wrappers)
    Texture2D heightmap;
    Texture2D normals;
    Matrix    transform;   // user-space translate; m12/m14 double as cull center
    float     size;        // world size for the cull AABB
    double    abs_x, abs_z;// absolute tile center, kept for offset rebuilds
    tile_key  key;         // backlink for eviction slot-fixups + debug labels
    bool      visible;     // frustum result, written in place by post_process
    bool      desired;     // desired-set membership, for the debug overlay
};
```

- **No separate user_x/user_z fields**: `MatrixTranslate` stores translation in `m12/m14`; cull and
  debug read those. One representation, no drift.
- **`desired` flag on the item** instead of exposing the manager's `desired_keys` set to the debug
  overlay — refreshed only when the desired set rebuilds (rare).

### Ownership split

`loaded_tile` (the resident map's value) shrinks to the *owners* plus the list backlink:
`raii::texture ×3`, `raii::image hm_image` (for `ground_height`), `uint32_t slot`. Everything
draw-related lives in the item. `slot` replaces a separate `slot_of` map — the resident map and the
slot index would always have identical key sets, so they are one structure.

### List maintenance (all in tiles_manager)

- **Promote**: `push_back` the item, `slot = size-1`. Transform baked from
  `abs + world_offset` in double, cast to float once (precision discipline preserved verbatim).
  `pre_process` gains the `world_offset` parameter (internal signature; streamer has it cached).
  If the key is somehow already resident, replace textures in the map and rewrite its existing slot
  in place (defensive; spawn/promote flow shouldn't produce this).
- **Evict** (inside the GC `erase_if`): swap-with-last, `pop_back`, fix the moved item's owner
  `slot` via its `key` backlink. Reading/mutating *other* map values during `erase_if` is legal;
  only the erased element's iterator is invalidated.
- **Cull** (`post_process`): iterate the contiguous list; if `world_offset` changed since last
  frame (rebase — rare), first rebake every transform; then `visible = is_tile_in_frustum(m12, m14,
  size, frustum)`. This drops the per-tile double-add that previously ran every frame.
- **Desired rebuild** (`process_current_location`): after the set is rebuilt, refresh `item.desired`
  for all items (only runs on movement > update threshold).

### Renderer (tiles_renderer)

```cpp
int  draw(const Vector3 &camera_position, std::span<const render_item> items);
static void debug(const Camera3D &camera, std::span<const render_item> items);
static void debug_3d(std::span<const render_item> items);
```

Draw loop: `for (item : items) if (item.visible) { bind 3 maps; DrawMesh(item.mesh, material,
item.transform); }`. No offsets, no maps, no lookups. Debug overlays read `m12/m14`, `size`,
`key.zoom`, `desired`.

### Streamer

`draw()` / `draw_debug_*` pass `tile_manager->render_items()` (a `std::span<const render_item>`).
`make_debug_view` and `data_view` are deleted.

## What deliberately does *not* change

- GC/eviction *policy* (desired/frustum/horizon/coverage rules) — only where the flags live.
- `ground_height` — still resident-map + `hm_image` (step 7 replaces the image with a grid).
- List ordering — insertion order for now; step 4 decides the sort policy.

## Risks / mitigations

- **Slot bookkeeping bugs** (the classic swap-remove pitfall). Mitigation: the invariant
  `rendering_tiles[item.key].slot == index_of(item)` is asserted in debug builds after GC
  (cheap linear sweep under `#ifndef NDEBUG`).
- **Stale transforms after rebase**: covered by the offset-change comparison (exact float compare is
  correct — rebase writes the same bits it compares against; no epsilon games).
- **Dangling handles**: items hold non-owning copies; eviction removes the item and the owner in the
  same operation, so no item can outlive its textures. The span is only valid within a frame —
  documented on `render_items()`.
- No unit-test coverage for this step (needs GL context for real textures) — relies on the step-1/2
  suites for policy, compile-time type discipline, and the author's visual checkpoint after this
  step (demo: normal flight + a rebase crossing + wireframe/labels overlays).

## Re-evaluation (pre-implementation)

- Considered SoA (separate visible[] / transforms[] arrays) — rejected for now: the draw loop touches
  every field of the visible items anyway, AoS is one stream, and items are ~150 bytes so a 600-tile
  list is ~90 KB — L2-resident either way. Revisit only with profiler evidence.
- Considered keeping `data_view` for the debug paths — rejected: two access paths to the same state
  is how the leak comes back; debug needs (`key.zoom`, `desired`, position) which the item now carries.
- Considered rebaking transforms lazily per item during cull — rejected: the rebase branch is
  once-per-offset-change; doing it as a separate tight loop keeps the common path branch-free-ish
  and obvious.

## Re-evaluation (post-implementation)

- All targets build warning-free; step-1/2 suites green (25 097 assertions).
- Walked the tricky paths by hand:
  - *GC before first cull of a new tile*: `visible` starts false, same as the old
    `in_frustum_this_frame` — and newly promoted tiles are desired, so the flag isn't consulted.
    Semantics unchanged.
  - *Swap-remove during `erase_if`*: the moved item's owner is always still in the map when its
    slot is re-pointed (it gets its own evict later in the pass if it's also going). The debug-build
    lockstep assert sweeps the whole list after GC.
  - *Rebake trigger* compares x/z only — transforms never used offset.y before either.
- Behavior deltas, all intended: per-frame double-add per tile → baked once per rebase; per-tile
  mesh hash lookup → none; debug label color now reads the item's `desired` flag (refreshed on
  desired rebuilds) instead of querying the live set — the overlay can lag one rebuild, acceptable
  for a debug HUD.
- **Author visual checkpoint (deferred, not blocking)**: run the demo — normal flight, cross a
  rebase boundary (4096 m), toggle wireframe (L) and labels (K). Expected: identical visuals to
  `a982ad7`, identical tile pop-in behavior. Each step is one commit, so any regression bisects
  trivially.
- Deviation from spec: none.
