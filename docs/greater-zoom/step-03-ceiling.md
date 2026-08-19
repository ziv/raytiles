# Step 3 — Ceiling 15 → 18, per-zoom arrays, `native_terrain_zoom`

**Status:** DONE

## Spec

- `max_supported_zoom = 18` (`zoom_levels` derives to 10 automatically); default
  `thresholds` extended `…2500, 1250, 625, 312`, `skirt_overlap` extended with `1.00f`;
  `lod::options` defaults kept in sync.
- `network_config::native_terrain_zoom = 15` — zoom above which heightmaps are synthesized and
  normals default (no HTTP for either above it, wired in step 4). Validated at `tile_source`
  construction: must lie in `[min_supported_zoom, max_supported_zoom]`.
- C mirror: `RAYTILES_ZOOM_LEVELS 10`, `RaytilesNetworkConfig.native_terrain_zoom` (ABI change —
  the eventual commit is `feat!:`).
- Default `world.max_zoom` **stays 15**: zoom >15 is opt-in; every existing lod
  snapshot/equivalence test must pass unchanged — that is the proof the defaults didn't move.
- New lod test: non-default options with `max_zoom = 17` against the in-test reference
  implementation (which is generic over options).

## Re-evaluation

- **The snapshots caught a real mistake during this step**: `world_config::max_zoom` (and
  `lod::options::max_zoom`) defaulted to `max_supported_zoom`, so raising the ceiling silently
  raised the defaults to 18 — exactly what the plan promised would not happen. Fixed by making the
  default a literal 15, decoupled from the ceiling; snapshots pass unchanged again.
- Second finding: a first version of the extended-range lod test probed at y=2, where the horizon
  cap (~5 km) rejects base tiles whose centers are ~46 km away — the desired set never subdivides.
  Pre-existing policy quirk (now noted in the test); the probe moved to a base-tile center at
  y=500, which does reach z16+.
- `native_terrain_zoom` validated in `tile_source::resolve` (throws outside [9, 18]).
