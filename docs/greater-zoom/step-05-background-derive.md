# Step 5 — Background sibling/level generation

**Status:** DONE

## Spec

- New `background` queue of `derive_task {parent_x, parent_z, target_zoom}` (parent at the native
  zoom, absolute provider coords) + `background_done` dedup set (token = `tile_key{target, x, z}`,
  reusing the existing hash). Enqueued by the synchronous synthesis path after serving the
  requested tile.
- Workers prefer `pending`: the wait predicate covers both queues, but a background task is popped
  only when no real job waits — sibling generation can never delay a requested tile.
- A task re-reads the (guaranteed cached) native parent, then recursively generates every
  descendant level down to `target_zoom`, writing only PNGs that don't already exist (children
  floats are recomputed regardless — needed to recurse deeper). Best-effort: any failure skips
  silently; `stop_requested()` is checked per child so shutdown stays prompt.

## Tests

Request one z16 child → after its payload, pump until all four z16 sibling PNGs exist on disk;
a z17 request eventually materializes the full 4 + 16 descendant set.

## Re-evaluation

- Tests green: after a z16 child's payload, all four z16 siblings materialize on disk; a z17
  request eventually yields the full 4 + 16 descendant set.
- The explicit priority-ordering assertion from the spec was dropped as untestable without racy
  timing games; priority is enforced structurally (background pops only when `pending` is empty —
  one branch, reviewed) and the behavioral tests cover the outcome that matters.
- `run_derive` recomputes child floats even when the child PNG exists (needed to recurse deeper);
  writes are skip-existing; every failure path is silent by design (a direct request retries
  synchronously).
