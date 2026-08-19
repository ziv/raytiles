# Step 6 — Close-out

**Status:** DONE

- `sandbox/demo.cpp` opts in: `max_zoom = 17`, `skirt_overlap` extended to 10 slots (a 7-slot
  brace-init would leave zeros → degenerate meshes at z16+; invariant now documented in CLAUDE.md).
- `docs/arch.md` gained a "Synthesis above the native zoom" subsection (end of §6); CLAUDE.md
  architecture diagram + invariants updated (ceiling 18, `native_terrain_zoom`, normals never
  fail, terrain_synth purity, hand-declared `stbi_write_png_to_mem` prototype).
- Full suite: 31 test cases / 29 991 assertions green; all targets build warning-free; bindings
  mode verified with a scratch configure+build.
- **Author checkpoints (pending)**: run the demo and descend low over the Negev anchor — sharper
  imagery with geometry following it, no seams between derived tiles, flat-ish lighting above z15
  (the accepted §1.6 trade-off), and `.cache/heightmap/16..17/` filling up in the background.
  Everything is uncommitted, awaiting review per the standing rule.
