# Greater Zoom Plan

Support zoom levels **above 15**. Imagery providers (Esri, Mapbox) serve textures well past z15,
but the Mapzen Terrarium heightmaps and normal maps stop at 15 — so today `max_supported_zoom = 15`
is a hard wall. This plan removes the wall by **synthesizing** heightmaps above the native ceiling
and by making normals non-fatal everywhere.

> **Execution status (2026-08):** implemented — all 6 steps (committed as `be87a81`), then the
> ceiling was raised again **18 → 22** at the author's request, which also forced the background
> backfill to switch from full-subtree to lineage-siblings (see §1.3). Per-step docs:
> [`greater-zoom/`](greater-zoom/README.md).

Decisions locked with the author (2026-08):
- **Ceiling: 18 for now** (§1.7) — *superseded: raised to 22 while testing; see the §1.7 addendum.*
  Making the ceiling truly dynamic (runtime/provider-driven instead of a compile-time constant) is
  a recognized follow-up, not part of this plan.
- **Normals: default flat normals** above the native zoom, and whenever HTTP doesn't return a valid
  image (§1.6). No normals derivation from parents in this plan.

## Requirements (from the original outline)

1. **Normals fallback** — a missing normals file must not fail the tile; use default normals,
   produced by a dedicated function.
2. **Height synthesis above z15** — cache first, as always. On a miss, derive from the native z15
   tile: fetch the z15 ancestor, generate its four z16 children, and deliver the requested child
   **as soon as it is ready** — the rest are created without blocking it. A z17 request derives
   through the chain (z15 → z16 quadrant → z17 quadrant); all intermediate/sibling tiles are
   generated in the background while the requested tile returns as fast as possible.

## Non-goals

- Texture synthesis (imagery exists natively at high zoom; a derive-from-parent fallback for
  regions where it 404s is a possible follow-up).
- Normals derived from the z15 parent above the native zoom — considered, decided against for now
  (§1.6); revisit if flat lighting above z15 turns out to look wrong in practice.
- A runtime-dynamic zoom ceiling — follow-up (§1.7).
- Changing the LOD policy, render list, or promotion machinery — this is almost entirely a
  `tile_source` feature; the store and renderer are untouched.
- Sky module.

---

## 1. Key design decisions

### 1.1 Interpolate heights, never Terrarium RGB

Terrarium encodes `h = r·256 + g + b/256 − 32768`. Adjacent heights can be *distant* in channel
space (`g` wraps 255→0 as `r` carries), so bilinearly filtering the PNG channels produces spikes at
carry boundaries. Derivation therefore runs: **decode parent → float heights → bilinear upsample →
re-encode Terrarium → PNG**. The float path also preserves the `b`-channel fraction that the
`uint16` query grid deliberately drops — derived tiles are as precise as their parent.

### 1.2 Where synthesis lives: inside `tile_source`, behind the cache

The worker's heightmap fetch for `zoom > native` becomes:

```
cache hit?  ──────────────────────────────► decode, done  (unchanged path)
cache miss ► ancestor at native zoom (cache-or-HTTP, cached as usual)
           ► decode to floats
           ► walk down the lineage, upsampling ONLY the quadrant containing the target
           ► encode target → atomic cache write → serve it in this job's payload
           ► enqueue a background task to generate the missing siblings/levels
```

Nothing above `tile_source` knows any of this happened: the store still requests keys and receives
payloads; the cache still holds one PNG per tile; a later request for a sibling is a plain cache
hit. Consequently **no HTTP is attempted for heightmaps above the native zoom** — the provider
doesn't have them, so we don't ask.

### 1.3 Requested tile first, siblings in the background

Two-priority work inside the source: the existing `pending` job queue, plus a new `background`
queue of derive tasks. Workers always prefer `pending`; background tasks run only when no real
job waits, so sibling generation can never delay a requested tile. A derive task is small —
`{parent native key, target zoom}` — and regenerates from the (now guaranteed cached) parent file,
skipping children whose PNGs already exist. Tasks are deduped by `(parent, target_zoom)`;
they are best-effort (no cancellation needed, dropped at shutdown by the stop token).

Rationale for pre-generating rather than deriving each sibling on demand: when the camera descends,
the desired set requests the siblings almost immediately — pre-generation turns those into cache
hits instead of N independent parent-decodes.

*Addendum (ceiling 22):* the original task generated the parent's **full subtree** down to the
target — 20 tiles at z17, but ~21 800 tiles (~1 GB) per parent at z22. With the ceiling at 22 the
task instead generates the **lineage siblings**: the 4 children of each node along the requested
tile's ancestry (28 PNGs at z22). Cousins outside the lineage derive on demand (~ms per tile,
served synchronously) and enqueue their own lineage backfill. Dedup is per requested tile.

### 1.4 Seams and alignment

Sampling is texel-center aligned: child texel `i` maps to parent coordinate
`quadrant·128 + (i + 0.5)/2 − 0.5`, clamped to the parent. Two derived siblings that share an edge
sample the same parent column/row, so shared edges are continuous by construction. Parent-tile
boundaries keep today's behavior (clamping; skirts hide the seams) — no worse than z15 today.

### 1.5 Default normals

`default_normals_image(size)` — a solid `RGB(128, 128, 255)` image, which the shader decodes to
the flat up-normal `(0, 0, 1)` (lighting falls back to purely ambient + sun-from-above).

### 1.6 Normals policy *(decided)*

Normals **never fail a tile**. The worker's normals path:

- `zoom > native_terrain_zoom` → check the disk cache first (a user may pre-bake normals); on miss,
  produce `default_normals_image` in memory. No HTTP, no cache write (defaults are cheaper to
  regenerate than to store).
- `zoom <= native_terrain_zoom` → cache → HTTP → decode as today, but **any** failure to end up
  with a valid image (404, network error, corrupt/undecodable bytes) yields the default instead of
  dropping the tile. Heightmap and texture failures keep today's semantics (drop + reason) —
  terrain geometry and imagery are load-bearing; normals are a lighting refinement.

This removes the need for a structured 404-vs-error fetch result: normals are catch-all, and the
other two assets keep throw-on-failure. (Deriving normals from the z15 parent was considered and
parked — see Non-goals.)

### 1.7 The ceiling *(decided: 18 → raised to 22 while testing; dynamic later)*

*Addendum:* now **22** (`zoom_levels` = 14; thresholds continue halving to
`…156, 78, 39, 20`). At z19–22 tile sizes are ~130/65/32/16 m with the 256-res mesh — far beyond
the data's real resolution; heights there are pure smooth magnification of z15. This is a
"see how it behaves" setting per the author; the original 18 rationale below stands as the
technical argument.

`max_supported_zoom` becomes **18**: z16/17/18 tile sizes ≈ 1037/518/259 m with the 256-res mesh ≈
4/2/1 m per vertex — already below the source data's real resolution, so deeper synthesis adds
cache and derive depth for no information. All per-zoom arrays resize with it:
`zoom_levels` 7 → **10**, public `thresholds`/`skirt_overlap` arrays and `RAYTILES_ZOOM_LEVELS`
(C header) follow. New default thresholds continue the halving: `…, 2500, 1250, 625, 312`.

**`config.world.max_zoom` default stays 15.** Zoom >15 is opt-in: derived terrain costs disk and
startup work, and keeping the default keeps every existing lod snapshot/equivalence test valid
byte-for-byte.

*Follow-up (out of scope):* replace the compile-time ceiling with a truly dynamic bound
(provider-declared native zooms, runtime-sized arrays). The config knob in §1.8 is the first brick.

### 1.8 Native ceiling is provider config, not a constant

`network_config` gains `native_terrain_zoom = 15` — the zoom above which heightmaps are synthesized
and normals default (no HTTP for either above it). Mapzen = 15; a future provider with z17
heightmaps just raises it. Textures are unaffected; they fetch natively at every zoom.

### 1.9 A latent bug this feature would trip: `write_atomic` tmp collisions

Today each cache path is written by at most one job (in-flight dedup by key). With background
derive tasks, a sibling's direct job and a derive task can write the **same path concurrently**;
both use `path + ".tmp"`, so interleaved writes would corrupt the file before the atomic rename.
Fix (prerequisite phase): unique tmp suffix per writer (`path + ".tmp" + thread-local counter`).
The rename race itself is benign — both writers produce identical bytes.

---

## 2. What changes where

| Area | Change |
|---|---|
| `include/raytiles/raytiles.h` | `max_supported_zoom` 15→18, `zoom_levels` 7→10, threshold/skirt defaults extended, `network_config::native_terrain_zoom` |
| `include/raytiles/craytiles.h` + `src/craytiles.cpp` | `RAYTILES_ZOOM_LEVELS` 10, `native_terrain_zoom` mirror (same commit, per wrapper rule) |
| `src/detail/terrain_synth.hpp` *(new, pure)* | float decode / quadrant upsample / Terrarium encode / `default_normals_image` — no raylib calls, worker-safe, unit-testable |
| `src/detail/tile_source.h` / `src/tile_source.cpp` | normals catch-all fallback, heightmap synthesis above `native_terrain_zoom`, background derive queue, PNG encode via stb_image_write prototypes (same linkage trick as stb_image), unique tmp suffix |
| `src/tile_store.cpp` | none (validation bound updates via the constant) |
| `tests/` | terrain_synth suite + tile_source synthesis/normals-fallback suites + extended-range lod case |
| `sandbox/demo.cpp` | opt-in: `conf.world.max_zoom = 17;` for the visual checkpoint |
| `docs/arch.md`, `CLAUDE.md` | synthesis paragraph in the tile_source section; invariants updated |

PNG encoding on workers uses `stbi_write_png_to_mem` — prototypes only, implementation linked from
raylib's `rtextures` object (raylib compiles stb_image_write for its export API), mirroring the
existing `stbi_load_from_memory` arrangement. raylib's own `ExportImage*` is **not** used
(TraceLog → not worker-safe).

---

## 3. Step-by-step execution

Same working process as the composition refactor: one step = spec-doc in `docs/greater-zoom/` →
re-evaluate → implement → build + tests green → re-evaluate → **ask before committing**.

### Step 1 — Pure synthesis helpers + tests *(no behavior change)*
`terrain_synth.hpp`: `decode_terrarium_floats(Image) → vector<float>`;
`upsample_quadrant(span<float>, w, h, qx, qz) → vector<float>` (texel-center bilinear);
`encode_terrarium(span<float>, w, h) → Image` (malloc'd pixels, UnloadImage-compatible);
`default_normals_image(size)`.
Tests: decode→encode round-trip exactness; carry-boundary values (heights like 255.996 where `b`
wraps); upsample of a linear ramp is exact; shared-edge continuity of two sibling quadrants;
flat-normal pixel check. ~200 LOC + tests. `refactor:`.

### Step 2 — Normals fallback + tmp-collision fix
Normals path becomes non-throwing per §1.6 (cache → HTTP → decode, any failure → default; above
native: cache → default, no HTTP). Unique tmp suffix in `write_atomic`. Tests: 404-normals →
payload carries flat normals; corrupt normals bytes → flat normals; 404-heightmap still drops;
timeout still drops. Requirement 1 fully lands here. ~120 LOC. `feat:`.

### Step 3 — Raise the ceiling constants *(API/ABI-visible, mechanical)*
`max_supported_zoom = 18`, `zoom_levels = 10`, extended default arrays,
`native_terrain_zoom` config + C mirror + validation (`native_terrain_zoom` within
`[min_supported_zoom, max_supported_zoom]`). Default `world.max_zoom` **stays 15**. lod tests gain
a non-default case at max 17; all existing snapshots must pass unchanged — that's the proof the
defaults didn't move. The array-size change alters the public C struct layout → `feat!:`.

### Step 4 — Synchronous height synthesis
Worker heightmap path for `zoom > native_terrain_zoom`: cache → ancestor (cache-or-HTTP) → float
decode → quadrant-chain upsample → encode → atomic write → serve. No HTTP above native for
heightmaps. Tests (offline): seed only a z15 gradient tile → request a z16 child → payload heights
match the bilinear expectation, child PNG cached; request z17 with only z15 seeded → chain works;
corrupt z15 → drop with reason. Requirement 2 (synchronous half) lands here. ~200 LOC. `feat:`.

### Step 5 — Background sibling/level generation
`background` queue + worker preference for `pending`; derive task `{parent key, target zoom}`
generating all missing descendants under that parent up to the target level (z17 request ⇒ 4×z16 +
16×z17 PNGs, ~2 MB, skip-existing); dedup set; drained by stop token. Tests: request one z16 child,
pump until all four sibling PNGs exist on disk; assert a queued *real* job completes before a
freshly enqueued background task (priority). Requirement 2 (background half) lands here.
~180 LOC. `feat:`.

### Step 6 — Close-out
Demo opt-in (`max_zoom = 17`) for the visual checkpoint (descend low over the Negev anchor:
sharper imagery, geometry follows, no seams between derived tiles; lighting above z15 is expected
to flatten — that's decision 1.6); `docs/arch.md` + `CLAUDE.md` updated; tracker swept.
`docs:`/`chore:`.

---

## 4. Risks / open questions

- **Disk growth**: each visited high-zoom parent adds up to 20 PNGs (z17 depth). Bounded by
  visited area; no eviction policy exists today for the cache generally — unchanged, just noted.
- **Derived-tile quality**: bilinear upsampling adds no information — z16+ heights are smooth
  magnifications of z15. That is the *point* (geometry keeps up with sharper imagery), but don't
  expect new terrain detail. The `height_scale` drama factor applies unchanged.
- **Flat lighting above z15** (accepted trade-off of decision 1.6): close-up terrain loses
  relief shading where the imagery is sharpest. If it bothers the eye in the demo, the parked
  derive-normals-from-parent option slots into the step-4 machinery with ~80 LOC.
- **`loading_progress` during synthesis**: derivation is fast (~1–3 ms/tile) and local, so
  high-zoom initial loads are dominated by texture HTTP exactly as before. No change needed.

---

*Original outline (2026-08, kept for reference): normals-missing → default-normals function;
height above 15 → cache first, else derive z16 children from the containing z15 tile, serving the
requested child immediately and creating the rest without blocking; z17 derives through the chain
with intermediate tiles generated in the background.*
