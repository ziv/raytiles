# Refactor execution tracker

Master plan: [`../refactor-plan.md`](../refactor-plan.md). One short doc per step in this directory;
each carries its own spec, re-evaluation notes, and status. A step is only marked **done** here after
implementation + re-evaluation + green build/tests + commit.

| # | Step | Doc | Status |
|---|------|-----|--------|
| 1 | Extract pure `lod` policy + real test suite | [step-01-lod-extraction.md](step-01-lod-extraction.md) | **done** |
| 2 | Per-zoom array, `get_loading` fix, dead-code removal | [step-02-small-fixes.md](step-02-small-fixes.md) | **done** |
| 3 | `render_item` list, renderer consumes span, delete `data_view` | [step-03-render-list.md](step-03-render-list.md) | **done** (visual check pending) |
| 4 | Draw-order policy (front-to-back on membership change) | [step-04-draw-order.md](step-04-draw-order.md) | **done** |
| 5 | `pool` → `tile_source` (one job/tile, ready queue, cancel by key) + tests | [step-05-tile-source.md](step-05-tile-source.md) | **done** |
| 6 | `tiles_manager` → `tile_store` (reconcile/promote/update_desired/cull) | [step-06-tile-store.md](step-06-tile-store.md) | **done** |
| 7 | Height grid (`uint16`) + bilinear `ground_height` | [step-07-height-grid.md](step-07-height-grid.md) | **done** |
| 8 | Public API v2 (nested `config`, raylib types) + C wrapper + sandbox + README | [step-08-api-v2.md](step-08-api-v2.md) | **done** |
| 9 | R16 height texture (**skipped** by decision 2026-08-19 — revisit post-refactor) | — | skipped |
| 10 | Close-out: CLAUDE.md sync, clang-format, plan status sweep | step-10-close-out.md | pending |

Working agreements (locked in with the author, 2026-08-19):
- One conventional commit per step on branch `refactor-composition-plan`; author reviews/pushes.
- Phase 8 is a full API break (`feat!:`), old API removed, `craytiles` flattened in the same commit.
- Verification = clean build of all targets + `run_tests`; author eyeballs demos at visual checkpoints
  (after steps 3, 5, 7, 8).
- The author's uncommitted `sandbox/demo.cpp` edits stay out of refactor commits until step 8 touches
  that file (coordinate then).
