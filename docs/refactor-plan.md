# Raytiles terrain refactor plan

> **Execution status (2026-08-19):** implemented on branch `refactor-composition-plan` — steps 1–8
> and 10 are done (one conventional commit each), step 9 (R16 height texture) was skipped by
> decision. Per-step specs, re-evaluations, and deviations live in [`refactor/`](refactor/README.md).
> §1 below describes the *pre-refactor* code and is kept as the historical rationale.

Scope: the **terrain module only** (`streamer`, `tiles_manager`, `pool`, `tiles_renderer`, `tile_shader`). The sky module is untouched. Each phase is independently shippable and leaves the build green; ordering matters — early phases build the safety net the later, riskier phases rely on.

Goals, in the author's words:

1. Separate concerns; prefer composition.
2. One data structure that contains everything needed to render — no per-draw lookups.
3. Less memory movement, fewer per-frame allocations and locks.
4. Simpler public API, aligned with raylib data structures.

---

## 1. Where the current implementation hurts

A quick inventory of the concrete problems the plan fixes. File references are to the current code.

### Separation of concerns

- `tiles_manager` is four responsibilities in one class: **LOD policy** (`build_required`, recursive subdivision), **residency/eviction** (`pre_process` GC + `is_tile_covered`), **GPU promotion** (`process_loaded_tiles`), and **height queries**. None of it is unit-testable because everything touches raylib and the download pool.
- The renderer doesn't own its input: `data_view` (`src/detail/utils.hpp:16`) hands the renderer *references to the manager's internal maps* (`rendering_tiles`, `tiles`, `desired_keys`). The draw path and the debug path share this leak. Any change to the manager's storage breaks the renderer.
- The frame steps are named by *when* they run (`pre_process` / `process` / `post_process`), not by *what* they do, so the orchestration in `streamer::update` reads as ceremony instead of policy.

### Lookups and memory movement in the hot path

- **Draw loop** (`tiles_renderer::draw`): iterates an `unordered_map` (pointer-chasing, cache-hostile order) and does a **hash lookup per tile per frame** (`draw_view.tiles.find(key.zoom)`) just to find the shared mesh. There is even a dead `draw_entry` struct in `tiles_renderer.h` from an abandoned sort attempt.
- **Promotion poll** (`process_loaded_tiles`): every frame, for every loading tile, three `shared_future::wait_for(0s)` calls — each takes an internal futex/mutex. With ~100 tiles in flight that is ~300 lock operations per frame that almost always answer "not ready".
- **Cancellation** (`pre_process`): for every loading tile that fell out of the desired set, `pool::cancel` is called **every frame until the download resolves**, and each call formats **three path strings** via `std::vformat` (heap allocations) and takes the pool mutex. The pool itself keys dedup/cancel on formatted path strings (`std::map<std::string, …>`, `unordered_set<std::string>`) — string hashing and allocation where a 12-byte `tile_key` would do.
- **Three jobs per tile**: texture/heightmap/normals are enqueued as three independent jobs with three promises. A tile is only usable when all three arrive, so the split buys nothing — it triples queue traffic, triples the cancellation surface, and creates the documented shared-future double-`get()` ownership hazard (`downloader.h:102`).
- **`tiles` keyed by zoom in an `unordered_map`** — at most 7 entries, already flagged `// todo replace with array (perf)`.
- **Heightmap retention**: each resident tile keeps its full decoded RGB(A) `Image` (256×256×3 ≈ 192 KB, ≈ 256 KB for RGBA) only so `ground_height` can read single pixels. At ~600 resident tiles that's ~115–150 MB of CPU RAM for a nearest-neighbor height lookup.
- `get_loading()` is wrong: `1 - loading/required` mixes "still loading" with "desired" (which includes already-resident tiles), so the progress bar jumps and can report 0 while loading.

### API friction

- Four config structs at the constructor, then **three internal mirror structs** (`tiles_manager_options`, `pool_options`, `tile_shader_options`) with field-for-field copy functions and triplicated doc comments (raytiles.h, tiles_manager.h, craytiles.h). Every new option touches ~6 places.
- `Plane` / `Frustum` are exposed in the public header but are pure implementation details.
- Shader parameters use raw `float[4]` / `float[3]` where raylib has `Color` / `Vector4` / `Vector3`; each setter exists in 3 overloads × 3 layers (streamer → tiles_renderer → tile_shader) = pure forwarding boilerplate (~120 lines).
- The vertex shader decodes Terrarium RGB per vertex — coupling the shader to the provider format twice (CPU and GPU).

---

## 2. Target architecture

Keep the good bones — the streamer/manager/renderer split, RAII wrappers, absolute-vs-user space discipline, budgeted uploads, worker-threads-never-touch-raylib — and reorganize around **one render-ready structure** with composition:

```
raytiles::streamer                      thin per-frame orchestrator (public API)
  ├── lod            (pure policy)      desired_tiles(options, position) -> vector<tile_key>
  │                                     no raylib, no I/O, no state → unit-testable
  ├── tile_source    (async I/O)        one job per tile fetches all 3 assets;
  │                                     delivers complete move-only payloads via a
  │                                     ready queue; cancel & dedup by tile_key
  ├── tile_store     (resident set)     owns GPU/CPU resources, promotion budget,
  │                                     eviction/coverage, ground_height,
  │                                     and maintains the render_list
  └── terrain_renderer (draw)           shader + material; consumes a const
                                        span<render_item> — nothing else
```

### 2.1 The render list — the "everything needed to render" structure

```cpp
// src/detail/tile.hpp
struct render_item {
    // raylib handles by value (they are small PODs — Texture2D is 5 ints,
    // Mesh is pointers+counts). Non-owning copies; ownership stays in
    // tile_store's resident slots. This is exactly how raylib itself
    // passes these around, so the renderer speaks native raylib.
    Mesh      mesh;        // shared per-zoom mesh
    Texture2D albedo;
    Texture2D heightmap;
    Texture2D normals;
    Matrix    transform;   // user-space translate, rebuilt on offset change
    float     size;        // world size, for culling AABB
    double    abs_x, abs_z;// absolute center (double!) for offset rebuilds
    tile_key  key;         // for debug overlays / eviction backlink
    bool      visible;     // frustum flag, updated in place by cull
};

std::vector<render_item> render_list;               // contiguous, AoS
std::unordered_map<tile_key, uint32_t> slot_of;     // key -> index, O(1) membership
```

Rules that make it fast and simple:

- **Membership changes are rare** (promotion/eviction — a handful per frame at most); the per-frame work is a linear scan. Insert = `push_back` + record index; erase = swap-with-last + fix the moved entry's `slot_of`. No per-frame rebuild.
- **Draw** becomes: `for (item : render_list) if (item.visible) { bind 3 maps; DrawMesh(item.mesh, material, item.transform); }` — zero hash lookups, zero pointer chasing, one branch per tile.
- **Cull** writes `visible` in place over the same contiguous array — the frustum test data (center, size) is already in the item; no map iteration.
- **Sort policy**: keep the list ordered coarse-zoom-first (or re-sort front-to-back only when membership changes / camera crosses a tile boundary). Terrain is overdraw-heavy; front-to-back order lets early-Z reject skirt/hidden fragments. Do *not* sort every frame — membership-change frequency is the natural cadence.
- `transform` bakes `abs + world_offset` (computed in double, cast to float once — preserving the current precision discipline in `tiles_renderer.cpp:53`). Recompute all transforms only when `world_offset` changes (rebases are rare: demo threshold is 4096 m), instead of per tile per frame.
- Debug overlays (`draw_debug_3d`, `draw_debug_labels`) read the same list — `data_view` disappears entirely.

`ground_height` data is deliberately **not** in the render list (it's a query structure, not a draw structure) — see §2.4.

### 2.2 `lod` — pure policy module (`src/detail/lod.hpp`)

Extract `process_current_location` + `build_required` + the horizon math into a free function:

```cpp
namespace raytiles::lod {
struct options {              // subset the policy actually needs
    int   base_zoom, max_zoom;
    float base_tile_size;
    int   rendering_radius;
    std::array<float, zoom_levels> thresholds;  // stored squared once
};
// deterministic in (options, position); appends into out to avoid realloc
void desired_tiles(const options&, Vector3 position, std::vector<tile_key>& out);
}
```

- No raylib calls (needs only `<cmath>` and `Vector3` — a POD), no I/O, no globals. This is what makes the doctest suite possible: structural invariants (no tile and its parent both present; everything inside the radius; children complete) and exact snapshot regressions of the desired set for fixed positions.
- `tile_store` diffs the returned set against residents/in-flight; policy never sees the store.
- Keep the output in a caller-owned reused vector + a reused `unordered_set` (or sort + binary search) so the ~1/500 m recompute allocates nothing in steady state.

### 2.3 `tile_source` — one job per tile, ready queue (`src/tile_source.{h,cpp}`)

Replace the promise/future-per-asset design:

```cpp
struct tile_payload {          // move-only; raii images free on every path
    tile_key key;
    raii::image albedo, height, normals;
};
class tile_source {
    void request(tile_key);                      // dedup by key
    void cancel(tile_key);                       // flips the job's atomic flag
    std::vector<tile_payload> drain_ready();     // swap under one lock
    std::vector<tile_key>     drain_failures();  // for store bookkeeping
};
```

- **One job fetches all three assets** sequentially on one worker (they are I/O bound and hit the same keep-alive clients). A tile arrives whole or not at all — the "all three futures ready?" poll, the partial-tile states, and the shared-future double-ownership hazard all disappear.
- **Ready queue**: workers push completed payloads into a mutex-protected vector; the store drains it with a single `swap` per frame. Per-frame synchronization cost: **one** uncontended lock, instead of 3 × N future polls. Failures go to a parallel queue so the store can clear `loading` state (current code silently forgets failed tiles until the next desired rebuild — keep that policy, but make it explicit).
- **Cancel by `tile_key`**: each job carries a `std::shared_ptr<std::atomic_bool>` cancel flag registered in a `unordered_map<tile_key, ...>`. The worker checks it between the three fetches (so a cancelled tile stops after at most one asset). No path-string formatting, no repeated cancels — the store cancels **once**, at desired-set rebuild time, not every frame.
- Keep unchanged: disk cache with atomic rename, per-worker keep-alive `httplib::Client` map, direct stb_image decode, `condition_variable_any` + `jthread` stop-token shutdown, httplib strictly confined to the .cpp.
- Optional micro-win: `stbi_load_from_memory` directly on the response body / file bytes as today, but reserve the read buffer from `Content-Length` and reuse a per-worker byte buffer across jobs to stop one 100–400 KB allocation per asset.

### 2.4 `tile_store` — resident set + render list + heights (`src/tile_store.{h,cpp}`)

`tiles_manager` renamed and slimmed to what it should be, with frame steps named for what they do:

```cpp
class tile_store {
    void reconcile(Vector3 abs_pos);          // eviction + cancel-once bookkeeping
    void promote();                           // drain source, upload under budget
    void update_desired(Vector3 abs_pos);     // run lod::desired_tiles, request missing
    void cull(const Frustum&, Vector3 offset);// write render_item.visible in place
    std::span<const render_item> render_items() const;
    std::optional<float> ground_height(Vector3 abs_pos) const;
};
```

Data layout changes:

- `tiles` (per-zoom metadata) → `std::array<zoom_level, zoom_levels>` indexed `zoom - base_zoom` (resolves the existing todo; `ground_height`'s per-zoom `.at()` hash lookups become array indexing).
- Resident tile ownership: `unordered_map<tile_key, resident_tile>` where `resident_tile` holds the `raii::texture` triple + height grid; the `render_list` holds the non-owning POD views (§2.1). The map is touched only on promote/evict/height-query — never in the draw loop.
- **Height grid instead of retained `Image`**: promotion decodes the Terrarium image once into `std::vector<uint16_t>` (raw `r*256+g+b/256` fixed-point, offset by 32768) — **128 KB vs 192–256 KB per tile (≈ 33–50 % of the height-query RAM back)**, and the query becomes an index into a typed array instead of format-branching pixel math. Add bilinear filtering while there (nearest-neighbor height on a 260 m/px zoom-9 tile produces visible steps for a landing aircraft). Free the decoded `Image` right after texture upload + grid build.
- Eviction (`reconcile`): keep today's rules (not desired → out-of-frustum / beyond-horizon / covered ⇒ evict; keep uncovered tiles to avoid holes), but run the *slow* coverage checks only when the desired set actually changed, not every frame; the every-frame part is just the cheap flag checks.
- Fix `get_loading()`: `loaded_desired / desired_total` over the desired set, monotonic during initial load.

### 2.5 `terrain_renderer` — draw only (`src/terrain_renderer.{h,cpp}`)

- Input: `std::span<const render_item>` + camera position. No `data_view`, no manager types, no maps.
- Keep the single shared `Material` with per-tile map swap (raylib rebinds per `DrawMesh` anyway; the realistic win is early-Z from draw order, not binding tricks). Delete the dead `draw_entry`.
- Collapse the setter-forwarding boilerplate: `terrain_renderer` exposes `tile_shader&` (or the streamer keeps one canonical setter set with raylib types only, §2.6) so the same parameter isn't plumbed through three classes.
- **Shader simplification (optional, measured)**: once promotion builds the height grid anyway, upload heights as `PIXELFORMAT_UNCOMPRESSED_R16` (half-float meters) instead of Terrarium RGB — one texture channel instead of three, the per-vertex decode drops to a single `texture().r`, and the provider format is confined to one CPU function. Caveat: half precision quantizes to ~4 m steps near 8848 m; visually irrelevant for displacement but verify against the snapshot scenes, or use `R32` (accepting 4 B/px vs 3) if it isn't. Keep `ground_height` on the uint16 grid either way — GPU format stops mattering to it.

### 2.6 Public API v2 — smaller and raylib-native

Breaking release (`feat!:`). Shape:

```cpp
namespace raytiles {

struct config {
    struct world_t     { int anchor_x, anchor_z; Zoom base_zoom, max_zoom;
                         float tile_size; std::array<float, zoom_levels> skirt_overlap;
                         bool mipmaps; Vector3 origin_offset; }    world;
    struct streaming_t { int radius; std::array<float, zoom_levels> thresholds;
                         float update_distance; double upload_budget_sec;
                         int max_uploads_per_frame; float near_plane, far_plane; } streaming;
    struct rendering_t { float fog_start, fog_end, skirt_drop;
                         Color fog_color   = SKYBLUE;      // raylib types, not float[4]
                         Color ambient     = WHITE;
                         Vector3 sun_direction = {0.1f, 1.0f, 0.1f};
                         float sun_scale, height_scale, normals_scale; } rendering;
    struct network_t   { int threads; bool allow_insecure_tls;
                         std::string texture_url, heightmap_url, normals_url;
                         std::string cache_dir = ".cache"; }        network;
};

class streamer {
    explicit streamer(const config& = {});
    streamer(double latitude, double longitude, config = {});
    void update(const Camera3D&, Vector3 world_offset = {});
    void draw();                                  // inside BeginMode3D
    std::optional<float> ground_height(Vector3) const;
    Vector3 initial_position(float altitude) const;
    bool  is_loading() const;  float loading_progress() const;
    void  set_rendering(const config::rendering_t&);   // one setter, whole struct
    // targeted hot setters kept: set_sun_direction(Vector3), set_fog_color(Color),
    // set_ambient(Color), set_height_scale(float) — raylib types only, one overload each
};
}
```

Decisions encoded here:

- **One nested `config`** replaces four positional structs *and* the three internal mirror structs: `tile_source` takes `const config::network_t&`, `tile_store` takes `world_t` + `streaming_t`, `terrain_renderer` takes `rendering_t`. Sub-structs are passed by reference to the components — the translation functions (`make_tiles_manager_options`, `make_pool_options`, `make_shader_options`) and their copy-drift risk are deleted. Docs live in exactly one place.
- **raylib types everywhere a raylib type exists**: `Color`, `Vector3`, `Vector4`, `Camera3D`, `Matrix`. Public exposure of `Plane`/`Frustum` moves to `src/detail/`. Update thresholds to plain meters (`update_distance`, squared internally) — no `MetersSq` in the public surface.
- **Overload trim**: one setter per parameter with the natural raylib type (the `float r,g,b,a` and `Vector4` variants go; `Color` covers the use case, `ColorNormalize` exists for the rest). Bulk changes go through `set_rendering`.
- `pool_config` URL/cache **path templates keep the current `:zoom:`/`{}` conventions** (documented, working) but cache paths derive from one `cache_dir` unless overridden — the three template strings become an advanced option, not the default surface.
- `craytiles.h`/`crayskies.h` mirror the change in the same commit (flatten `config` into one C struct + `RaytilesConfigDefault()`), and `demo_c.c` is updated with it — per the existing 1:1 wrapper rule.

---

## 3. Step-by-step execution plan

Each step = one PR, conventional-commit titled, CI green, demos run. Estimated sizes are for review scoping. **Note CONTRIBUTING.md: LLM-generated code is not accepted — this plan is a guide for hand-written changes.**

### Phase 0 — safety net *(no behavior change)*
1. **Extract `lod.hpp` + real tests** (`refactor:` + `test:`). Move `build_required` / desired-set / horizon math into pure `lod::desired_tiles`; `tiles_manager` calls it. Add `tests/lod_tests.cpp` (invariants + exact snapshots for 3–4 camera positions/altitudes) and `tests/utils_tests.cpp` (Terrarium decode round-trip, `tile_key` hash sanity). Wire into `run_tests`, delete the placeholder test. *This is the regression harness every later phase leans on.* ~300 LOC.
2. **`tiles` map → per-zoom array**; fix `get_loading()`; delete dead `draw_entry`; fix comment typos. `fix:`/`refactor:`. ~80 LOC.

### Phase 1 — the render list
3. **Introduce `render_item` + `render_list` in the manager; renderer consumes `span`** (`refactor:`). Promotion appends, eviction swap-removes, `post_process` writes `visible` in place, transforms rebuilt only on offset change. Debug draws read the list; **delete `data_view`**. Verify with demo + baseline target (initial-load wall time & FPS before/after). ~350 LOC.
4. **Draw-order policy** (`perf:`): maintain the list front-to-back on membership change. Measure with the baseline scene; keep only if it wins. ~60 LOC.

### Phase 2 — the source
5. **Rewrite `pool` → `tile_source`** (`refactor:`): one job per tile, `tile_payload`, ready/failure queues drained once per frame, cancel-by-key checked between assets, cancel issued once at desired-rebuild. Delete futures, path-string cancel set, per-frame `vformat` churn. Add `tests/tile_source_tests.cpp` (pre-seeded disk cache → payload arrives; corrupt PNG → failure queue; unreachable host → failure; cancel before pickup → no payload). This is the highest-risk step — land it alone. ~500 LOC.

### Phase 3 — the store
6. **Rename/slim `tiles_manager` → `tile_store`** with `reconcile / promote / update_desired / cull` (`refactor:`). Move desired-diffing here; gate coverage-eviction on desired-set change; streamer orchestrates the four steps by name. ~200 LOC, mostly moves.
7. **Height grid** (`perf:`): decode Terrarium → `vector<uint16_t>` at promotion, free the CPU `Image`, bilinear `ground_height`. Snapshot-test the sampler against `get_height_from_image` on a real cached tile. ~150 LOC.

### Phase 4 — the API
8. **Public API v2** (`feat!:`): nested `config`, raylib types, `Plane`/`Frustum` to detail, setter trim, delete the three mirror-option structs, update `craytiles` + `crayskies` boundary types + all sandbox apps + README/wiki snippets. ~600 LOC touched, mechanical but wide.
9. **Optional, measured** (`perf:`): R16 height texture + shader single-channel sample (§2.5 caveat). Only after (7), only with before/after captures of the snapshot scenes.

### Phase 5 — close out
10. Update `CLAUDE.md` architecture/tests sections to the landed state; delete this plan's "current pain" section or mark items done; run `clang-format` repo-wide on touched files.

### Explicit non-goals (rejected for now)
- **`DrawMeshInstanced` / texture arrays**: per-tile textures make instancing require array textures + shader surgery; the draw call count (≤ ~600, typically ~150 visible) doesn't justify it.
- **Full rewrite from scratch**: the threading rules, precision discipline, cache format, and eviction heuristics encode hard-won invariants; the redesign above reuses them all. A rewrite would re-derive them the painful way for the same destination.
- **In-flight HTTP abort**: httplib can't cancel a blocking `Get`; destructor may still wait out timeouts. Documented, acceptable.
- Sky module changes.

---

## 4. What "done" looks like

| Metric | Today | Target |
|---|---|---|
| Hash lookups in draw loop | 1 per tile per frame + map iteration | 0 (linear scan) |
| Per-frame sync ops for downloads | 3 × in-flight-count future polls + N × 3 vformat cancels | 1 lock (queue swap), cancels once per rebuild |
| Jobs / promises per tile | 3 / 3 | 1 / 0 |
| CPU RAM per resident tile for height queries | 192–256 KB (`Image`) | 128 KB (`uint16` grid) |
| Public config surfaces to document | 4 public + 3 mirror structs | 1 nested struct |
| Unit tests | 1 placeholder assert | lod invariants + snapshots, utils, tile_source |
| Renderer's knowledge of manager internals | 3 borrowed maps (`data_view`) | `span<const render_item>` |