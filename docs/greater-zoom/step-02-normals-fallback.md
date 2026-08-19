# Step 2 — Normals catch-all fallback + `write_atomic` tmp fix

**Status:** DONE

## Spec

- Normals never fail a tile (plan §1.6): the worker's normals fetch becomes non-throwing — cache →
  HTTP → decode as before, but any failure (404, refused, timeout, corrupt bytes) yields
  `synth::default_normals_image(256)` instead of dropping the tile. Heightmap/texture keep
  throw-on-failure (load-bearing assets). The above-native no-HTTP gate arrives with the
  `native_terrain_zoom` config in steps 3–4.
- `write_atomic` gets a unique tmp suffix (atomic counter) so two writers targeting the same cache
  path (possible once background derive tasks exist, step 5) can't interleave into one `.tmp`.

## Tests

Local-server harness: normals route returns 404 → payload arrives with flat default normals
(pixel-checked); corrupt normals bytes seeded in cache → default; heightmap 404 still drops.

## Re-evaluation

- Tests green: 404-normals → flat default payload (pixel-checked), corrupt cached normals →
  default, 404-heightmap still drops. The catch-all lives in a dedicated `fetch_normals` lambda
  (moved there in step 4 when the native gate arrived).
- `write_atomic` tmp uniqueness via a process-wide atomic counter — simpler than thread-ids and
  sufficient (uniqueness, not identity, is the requirement).
