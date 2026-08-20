# Greater-zoom execution tracker

Master plan: [`../greater-zoom-plan.md`](../greater-zoom-plan.md). One doc per step; a step is
marked done only after implementation + green build/tests + re-evaluation. Per the author's
standing rule, **nothing is committed without explicit permission** — the whole feature lands in
the working tree for review first.

| # | Step | Doc | Status |
|---|------|-----|--------|
| 1 | Pure synthesis helpers (`terrain_synth.hpp`) + tests | [step-01-terrain-synth.md](step-01-terrain-synth.md) | **done** |
| 2 | Normals catch-all fallback + `write_atomic` tmp fix | [step-02-normals-fallback.md](step-02-normals-fallback.md) | **done** |
| 3 | Ceiling constants 15→18, arrays, `native_terrain_zoom` config + C mirror | [step-03-ceiling.md](step-03-ceiling.md) | **done** |
| 4 | Synchronous height synthesis above native zoom | [step-04-height-synthesis.md](step-04-height-synthesis.md) | **done** |
| 5 | Background sibling/level generation queue | [step-05-background-derive.md](step-05-background-derive.md) | **done** |
| 6 | Close-out: demo opt-in, arch.md/CLAUDE.md, tracker sweep | [step-06-close-out.md](step-06-close-out.md) | **done** |
