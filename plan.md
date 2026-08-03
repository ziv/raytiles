# Raytiles refactor plan — composition, fewer moving parts, simpler API

Status: **implemented** (steps 1–7 landed on `fbl-refactor`, one commit per step).
Scope: internal architecture, data flow (memory moves/copies), public API surface, Emscripten removal.
Hard constraint: **default behavior must be preserved** — a zero-argument `streamer` (or `streamer(lat, lon)`) must produce the same world as today, with the same providers, zoom range, thresholds, and visuals.

---

## 1. What we have today (and what hurts)

```
streamer (facade + frame cache + gating state)
  ├── tiles_renderer          forwarding shim + draw loop
  │     └── tile_shader       GLSL + uniform setters
  └── tiles_manager           lifecycle: desired / loading / rendering
        └── pool              4 worker threads, promise/shared_future<Image> per asset
```

### Pain points

**P1 — Config translation layer (pure duplication).**
Four public structs (`world_config`, `streaming_config`, `rendering_config`, `pool_config`) are copied field-for-field into three internal structs (`tiles_manager_options`, `tile_shader_options`, `pool_options`) by `make_tiles_manager_options`, `make_shader_options`, `make_pool_options`. Every field exists (with duplicated doc comments) in two places; adding a knob touches 4–6 files.

**P2 — Setter forwarding boilerplate.**
`streamer` → `tiles_renderer` → `tile_shader`: 12 shader parameters × up to 3 overloads, each forwarded through two layers ≈ **70+ trivial functions** across `raytiles.h/.cpp`, `tiles_renderer.h/.cpp`, `tile_shader.h/.cpp` — plus the same surface mirrored again in `craytiles`. `tiles_renderer` exists almost entirely to forward.

**P3 — Per-asset futures: polling, string keys, shared ownership of malloc'd memory.**
Each tile spawns **three** jobs (`texture`, `heightmap`, `normals`), each with its own `std::promise<Image>` / `shared_future<Image>`:
- Main thread polls `wait_for(0s)` on 3 futures × every loading tile × every frame.
- Dedup and cancellation are keyed by **cache-path strings**, so `pre_process` calls `cancel()` (3 × `std::vformat` string builds + 3 map lookups) for every undesired loading tile **every frame**.
- `shared_future<Image>` shares a POD holding a malloc'd pixel buffer; correctness relies on "callers only `.get()` once" (documented as a known race/subtlety in `downloader.h`).
- Worker failure is delivered by exception through the promise, caught on the main thread per-future.

**P4 — Split frame state / temporal coupling.**
`streamer` keeps `last_position`, `last_frustum`, `near/far`, `update_distance_sq`, cached camera/offset; `tiles_manager` exposes `pre_process` → `process` → `post_process` that must be called in exactly that order with exactly the right frames of reference. The three-phase protocol is an internal detail leaking upward.

**P5 — Emscripten dual paths.**
`#ifdef __EMSCRIPTEN__` branches in `downloader.cpp` (emscripten_fetch vs httplib), `tile_shader.cpp` + `sky_shader.cpp` (GLSL version header), CMake (FETCH=1, pthread flags, no OpenSSL), plus a dedicated CI workflow. Every networking change must compile under both paths.

**P6 — Small inefficiencies / dead weight.**
- `tiles` is `unordered_map<Zoom, tile_value>` hash-looked-up in hot loops (`ground_height`, `draw`, `build_required`) — the header already carries a `// todo replace with array (perf)`.
- `tiles_renderer::draw_entry` (sort-by-distance struct) is declared but unused.
- `make_debug_view` rebuilds a `data_view` up to 3× per frame.
- `get_loading()` math is wrong when downloads finish between frames (progress can move backwards; division uses `loading/required` even when `desired` shrank).

---

## 2. Target architecture — composition of four small parts

`streamer` stays the only public class, but internally becomes a thin composition of components with **one responsibility and one data direction each**. No component knows about the one above it.

```
streamer                        thin facade: frame orchestration only
  ├── tile_source               "give me tile K" → complete tile_payload later (async)
  │                               owns: worker threads, http clients, disk cache, PNG decode
  ├── lod                       pure policy: camera position → desired tile-key set
  │                               owns: nothing (free functions + options struct; unit-testable, no GL)
  ├── tile_store                resident set: promotion (GPU upload w/ budget), eviction,
  │                               coverage checks, ground_height sampling
  │                               owns: meshes per zoom, loaded tiles, CPU heightmaps
  └── terrain_renderer          draw + shader uniforms (absorbs today's tile_shader;
                                  tiles_renderer as a separate layer is deleted)
```

Data flows one way per frame:

```
camera ──▶ lod ──▶ desired set ──▶ tile_store.reconcile()
                                      │ missing keys ──▶ tile_source.request(key)
tile_source.drain() ──▶ ready payloads ──▶ tile_store.promote()  (budgeted GPU upload)
frustum ──▶ tile_store.cull() ──▶ visible span ──▶ terrain_renderer.draw()
```

### 2.1 `tile_source` — one job per tile, delivery by queue, zero shared ownership

Replaces `pool`. Key changes:

- **One job per `tile_key`**, not three. A tile is only usable when all three assets are present, so a single worker fetches texture + heightmap + normals sequentially (cache-hit reads are microseconds; HTTP reuses the same keep-alive clients). This deletes the per-asset promise/future/cancel/dedup machinery outright.
- **Delivery is a ready-queue, not futures.** Worker pushes a completed payload into a mutex-guarded vector; the main thread calls `drain()` once per frame which does a single lock + `std::vector::swap` (O(1), never blocks on I/O). No polling, no `wait_for`, no exceptions crossing threads (failures are logged in the worker and the key is simply reported back as `failed` so the store can forget it).
- **Everything is keyed by `tile_key`** — `unordered_set<tile_key> in_flight` and `unordered_set<tile_key> cancelled`. No path-string maps, no `vformat` on the cancel path. `cancel(key)` is a set insert.
- **Images are move-only, single-owner.** New `struct pixel_buffer` (or reuse `raii::image` with an `stbi_image_free` deleter): decoded by the worker, **moved** into the payload, moved out of the queue, moved into the store. Zero copies of pixel data anywhere; ownership is unambiguous at every step.

```cpp
struct tile_payload {                       // built entirely off-thread
    tile_key key;
    raii::image albedo, height, normals;    // move-only, stb-allocated
};

class tile_source {
public:
    explicit tile_source(source_options);   // threads, urls, cache paths, tls
    void request(const tile_key&);          // no-op if already in flight
    void cancel(const tile_key&);           // best-effort, cheap (set insert)
    std::vector<tile_payload> drain();      // main thread, once per frame: lock + swap
    std::vector<tile_key> drain_failures(); // keys to drop (no retry — see §9.3)
    bool idle() const;                      // for loading-progress
};
```

`tile_source` never includes raylib beyond the `Image` POD it fills, and never calls raylib functions off-thread (unchanged invariant). httplib stays confined to `tile_source.cpp` (unchanged invariant: forward-declared / pimpl'd out of any header).

What this deletes: `std::promise`, `std::shared_future`, `in_flight_images` (string map), `cancelled_jobs` (string set), `enqueue_texture/heightmap/normals`, `cancel_texture/heightmap/normals/load`, `loading_tile` (the 3-future struct), the per-frame 3×N future polling loop, and the promise-exception plumbing.

### 2.2 `lod` — desired-set computation as a pure module

Extract `process_current_location` / `build_required` / horizon math into free functions over a small options struct:

```cpp
namespace lod {
    struct options { int base_zoom, max_zoom; float base_tile_size;
                     int radius; std::array<float, zoom_levels> thresholds; };
    // deterministic, no GL, no I/O — trivially unit-testable
    void desired_tiles(const options&, Vector3 abs_position,
                       std::unordered_set<tile_key>& out);
}
```

This is the piece with the subtle math (quadtree subdivision, horizon cutoff, LOD hysteresis) and today it is untestable without a window. Making it pure is the single biggest testability win, and it costs nothing at runtime.

### 2.3 `tile_store` — resident set, promotion, eviction, height sampling

Absorbs the rest of `tiles_manager` and owns everything with a lifetime: per-zoom meshes, `rendering_tiles`, CPU heightmaps.

```cpp
class tile_store {
public:
    tile_store(store_options);                                  // builds per-zoom meshes
    // one call per frame instead of pre_process/process/post_process:
    void reconcile(const std::unordered_set<tile_key>& desired, // evict + request-diff
                   Vector3 abs_position, tile_source&);
    void promote(std::vector<tile_payload>&&, double budget_sec, int max_count);
    void cull(const Frustum&, Vector3 world_offset);            // sets visibility flags
    std::optional<float> ground_height(Vector3 abs_position) const;
    // renderer reads tiles through a lightweight span/view (replaces data_view churn)
    const auto& visible() const;
    float progress() const;                                     // monotonic 0..1
};
```

Internal fixes rolled in:
- `tiles` map → `std::array<tile_lod, zoom_levels>` indexed `[zoom - base_zoom]` (kills hash lookups in `ground_height`, `draw`, and subdivision — the existing `todo`).
- `loaded_tile` stores a pointer/index to its zoom entry so `draw()` does zero lookups.
- Promotion consumes `tile_payload` by move; upload order/budget logic unchanged (`upload_budget_sec`, `max_uploads_per_frame` defaults preserved).
- Eviction (`is_tile_covered`, horizon check, GC) unchanged in behavior, just relocated; the per-frame *cancel spam* disappears because reconcile diffs desired-vs-in-flight once per **stream update**, not per frame.
- `progress()` computed from `loaded / (loaded + outstanding)` with a high-water mark so it can't run backwards.

### 2.4 `terrain_renderer` — absorb the shim

`tiles_renderer` (the forwarding layer) is deleted. `tile_shader` grows the draw loop and becomes `terrain_renderer`: shader, material, `draw(visible_tiles, world_offset, camera_pos)`, debug draws, and **one** setter per uniform (no per-layer duplication — `streamer` forwards once, see §4). The unused `draw_entry` struct goes.

### 2.5 `streamer` — what remains

```cpp
void streamer::update(const Camera3D& cam, Vector3 offset) {
    cache_frame(cam, offset);
    const Vector3 abs = cam.position - offset;
    store_.promote(source_.drain(), cfg_.upload_budget_sec, cfg_.max_uploads);
    if (moved_far_enough(abs)) {                 // update_distance gate, as today
        lod::desired_tiles(lod_opts_, abs, desired_);
        store_.reconcile(desired_, abs, source_);
    }
    store_.cull(utils::extract_frustum(cam, near_, far_), offset);
}
```

Roughly 20 lines of orchestration; every piece below it is testable in isolation. The `pre/process/post` protocol, the split gating state, and `make_debug_view` all disappear.

---

## 3. Memory-movement summary (before → after)

| Data | Today | After |
|---|---|---|
| Decoded pixels | promise → shared_future (shared POD w/ malloc'd buffer) → `.get()` struct copy → `raii::image` wrap | moved once: worker → payload → store. Single owner throughout |
| Ready detection | poll 3 futures × N loading tiles × every frame | one lock + vector swap per frame |
| Dedup / cancel keys | formatted path strings, `std::map<string,…>` | `tile_key` sets (existing SplitMix64 hash) |
| Config | 4 public structs copied into 3 internal structs (field-for-field) | public structs passed by `const&`/moved into components directly; internal mirror structs deleted |
| Per-zoom metadata | `unordered_map<Zoom, tile_value>` hashed in hot loops | flat `std::array`, direct index |
| Draw view | `data_view` of 4 references rebuilt up to 3×/frame | store exposes a stable view; built once |

Pixel buffers themselves were never deep-copied today — the wins are eliminating *shared ownership* (the documented shared_future race), *polling*, and *string churn*, plus the struct-copy layers around configs.

---

## 4. Public API simplification (defaults preserved)

### 4.1 One config, nested, all-defaulted

Keep the four groups (they document themselves well) but nest them so the constructor takes one thing, and rename `pool_config` to what it is:

```cpp
struct config {
    world_config     world{};       // unchanged fields & defaults
    streaming_config streaming{};   // unchanged defaults; see tweaks below
    rendering_config rendering{};   // unchanged fields & defaults
    network_config   network{};     // = today's pool_config, renamed
};

class streamer {
    explicit streamer(config cfg = {});                     // same world as today
    streamer(double lat, double lon, config cfg = {});      // same as today
    ...
};
```

C++ designated initializers keep call sites clean: `streamer s({.streaming = {.rendering_radius = 8}});`
Defaults preserved verbatim: provider URLs (incl. the intentional Esri `zoom/y/x` order), zoom range [9,15], thresholds, skirt overlaps, cache paths, 4 download threads, mipmaps on.

### 4.2 Field-level tweaks (behavior-neutral)

- `update_distance_sq` → `update_distance` (meters); squared internally. Default `500.0f` — identical behavior.
- Fix the `download_threads` doc/value mismatch (doc says 2, default is 4 — keep 4, fix the comment).
- `world_config::offset` todo: either document it as "initial position hint" (it only feeds `get_initial_position`) or fold it into the lat/lon constructor path where it's computed anyway.
- Per-zoom arrays (`thresholds`, `skirt_overlap`) stay — they're genuinely per-zoom tuning and the defaults must hold — but validation moves to `config` so errors fire before any GL resource is created.

### 4.3 Runtime setters: 12×3 overloads → 5 grouped setters

Replace the overload matrix with grouped setters taking raylib `Color`/`Vector3` (the types every raylib user already has). Each is a *single* function, forwarded once to `terrain_renderer`:

```cpp
void set_ambient_light(Color c);
void set_fog(Color c, float start, float end);      // also: keep set_fog_color(Color) for the common case
void set_sun(Vector3 direction, float intensity);
void set_height_scale(float);
void set_normals_scale(float);
```

Drop the `Vector4` and `(r,g,b,a)` float overloads — `Color` is the only accepted color type. It covers the use cases in every sandbox/demo, and float-precision colors can go through `rendering_config` at construction.

### 4.4 Misc surface cleanup

- `is_loading()` + `get_loading()` → single `float loading_progress()` (`>= 1.0f` means done) — plus keep `is_loading()` as a one-liner since both demos use it. Fix the backwards-progress bug while here.
- `draw_debug_3d()` + `draw_debug_labels()` stay (they're called in different render phases — 3D vs 2D — so merging them is wrong).
- `pre_process/process/post_process`, `make_debug_view`, `loading_count` were never public — they simply cease to exist.
- `get_initial_position(y)` stays (used by demo).

### 4.5 C wrapper follows 1:1 (unchanged rule)

`craytiles.h` mirrors the new surface: `RaytilesConfig` with nested structs + `RaytilesConfigDefault()`, grouped setters, same opaque handle. Net effect is a **smaller** C API (the setter matrix shrinks ~3×). `demo_c.c` updated in the same change, per CLAUDE.md.

---

## 5. Emscripten removal

Delete web support entirely (per decision). Checklist:

- `src/downloader.cpp` — remove `#ifdef __EMSCRIPTEN__` fetch path + includes; httplib becomes the only transport. (Goes away naturally when the file becomes `tile_source.cpp`.)
- `src/tile_shader.cpp`, `src/sky_shader.cpp` — `GLSL_VERSION_HEADER` collapses to `"#version 330\n"`.
- `CMakeLists.txt` — remove the `if (EMSCRIPTEN)` branch (FETCH=1, pthread flags, idbfs, WebGL flags, Sandbox `.html` suffix); httplib/OpenSSL become unconditional.
- `.github/workflows/emscripten.yml` — delete the workflow.
- `sandbox/main.cpp` — remove the emscripten main-loop shim.
- `README.md`, `CLAUDE.md` — drop the Emscripten build-mode docs and the "new networking code must compile under both paths" invariant.
- (`CHANGELOG.md` is release-please-generated — leave it.)

This also removes the `-fexceptions`/pthread-pool constraints and lets `tile_source` assume real threads + httplib unconditionally, which is what makes the §2.1 design clean.

---

## 6. File layout after

```
include/raytiles/raytiles.h      config + streamer (much shorter)
include/raytiles/craytiles.h     C mirror
src/streamer.cpp                 facade (renames raytiles.cpp)
src/craytiles.cpp
src/tile_source.cpp              replaces downloader.cpp
src/tile_store.cpp               replaces tiles_manager.cpp
src/terrain_renderer.cpp         replaces tiles_renderer.cpp + tile_shader.cpp
src/detail/tile_source.h         (httplib stays out of all other TUs — unchanged rule)
src/detail/tile_store.h
src/detail/terrain_renderer.h
src/detail/lod.hpp               pure policy (header-only)
src/detail/tile.hpp              tile_key, tile_payload, loaded_tile
src/detail/raii.hpp              unchanged (+ image deleter note)
src/detail/utils.hpp             frustum/height helpers (data_view removed)
```

Sky (`rayskies`) is untouched by this refactor except the GLSL header cleanup.

---

## 7. Implementation order (each step compiles green & keeps demos working)

1. **Emscripten removal** (§5). Pure deletion, zero design risk, unblocks everything else. `fix!:` or `feat!:` commit (it drops a supported platform → major-ish signal for release-please).
2. **Extract `lod`** as pure functions + add the first real doctest suite against it (desired-set snapshots at known camera positions, subdivision boundaries, horizon cutoff). This locks in current behavior *before* touching the loader.
3. **`tile_source` rewrite** (one-job-per-tile, ready queue). `tiles_manager` adapts internally; behavior verified against `baseline` target (initial-load wall-time) and visual demo parity.
4. **`tile_store`** (fold pre/post/process into reconcile/promote/cull; flat zoom array; progress fix). Delete `data_view`.
5. **Merge renderer layers** into `terrain_renderer`; delete `tiles_renderer`.
6. **Public API change** (`config`, grouped setters, `update_distance`) + `craytiles` + `demo_c` + sandboxes + README/wiki snippets in one commit (`feat!:` — this is the breaking release).
7. Sweep: clang-format, doc comments, CLAUDE.md architecture section update.

Steps 1–5 are internal (no public-surface break); only step 6 is breaking, so it lands as one reviewable commit with the migration notes in its message.

## 8. Testing plan

- `lod` unit tests (step 2) — the only currently-untestable-but-pure logic; no window needed.
- `tile_source` tests with a `file://`-style trick: point URL templates at a local dir and cache paths at a temp dir → exercise cache-hit, cancel, failure paths without network (workers hit the disk-cache branch).
- `tile_key` hash / `get_height_from_image` (Terrarium decode) unit tests — pure, cheap, currently uncovered.
- `baseline` sandbox remains the perf regression gate (initial-load wall time before/after step 3).
- Store/renderer stay covered by the demos (GL-bound, as today).

## 9. Resolved decisions

1. **Setters are `Color`-only** — no float/`Vector4` overloads (§4.3).
2. **`pool_config` is renamed to `network_config`** (§4.1).
3. **Failed tiles keep today's behavior** — dropped until the next desired-set rebuild, no retry/backoff. A richer failure policy (retry, backoff, per-asset fallbacks) is an explicit follow-up after this refactor lands.
4. **One job fetches all three assets sequentially** (§2.1). Cold-cache serialization (~3 RTTs) is expected to be invisible with 4 workers + keep-alive; verified against the `baseline` wall-time gate at step 3. If measurements disagree, the fallback is issuing the 3 GETs over pooled connections while still delivering a single payload.