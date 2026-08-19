# Step 5 — `pool` → `tile_source`: one job per tile, ready queue, cancel by key

**Status:** DONE
**Commit:** `refactor: rewrite the downloader as tile_source with a per-tile payload queue`

## Bug found while speccing

**Cancellation today is a silent no-op.** `pre_process` calls
`pool::cancel(key.zoom, key.x, key.z)` with *anchor-relative* coords; the pool's dedup/cancel maps
are keyed by cache-path strings formatted from *absolute* provider coords (`spawn` adds
`anchor * scale` before enqueueing). The paths never match (unless the anchor is 0), so
`cancelled_jobs` never gains an entry that could take effect. Nobody noticed because dropped-out
tiles also resolve normally and get discarded at promote time. The rewrite keys cancellation by
`tile_key` end-to-end, making cancel real for the first time — a behavior *fix*, noted here because
bandwidth use will visibly drop when flying fast.

## Design (plan §2.3)

New `src/detail/tile_source.h` + `src/tile_source.cpp`, replacing `downloader.{h,cpp}`:

```cpp
struct tile_request { tile_key key; int x, z; };        // x/z absolute provider coords at key.zoom
struct tile_payload { tile_key key; raii::image albedo, height, normals; };  // move-only

class tile_source {
    void request(const tile_request&);                  // dedup by key, no-op if in flight
    void cancel(const tile_key&);                       // flips the job's atomic flag
    struct drop { tile_key key; bool cancelled; std::string reason; };
    void drain(std::vector<tile_payload>& ready_out, std::vector<drop>& dropped_out);
};
```

- **One job fetches all three assets sequentially** on one worker (cache-read or HTTP + atomic
  cache write + stb decode, all unchanged mechanics). The cancel flag is checked when the job is
  picked up and between assets, so a cancelled tile wastes at most one asset fetch.
- **A tile arrives whole or not at all**: success → one `tile_payload` in the ready vector;
  network/decode failure or cancellation → one `drop` (with `reason` for main-thread logging —
  workers still never touch raylib/TraceLog). Partial images on the failure path are freed with
  `stbi_image_free` directly (not raii) so *no raylib call ever happens on a worker*, not even
  UnloadImage. `raii::image` wrapping happens only for payloads, which are destroyed on the main
  thread (drain or `~tile_source`).
- **`drain` = clear + swap of both vectors under one lock.** Per-frame synchronization drops from
  3 × in-flight future polls + per-frame vformat cancels to exactly one uncontended lock.
- `in_flight: unordered_map<tile_key, shared_ptr<atomic_bool>>` covers dedup + cancellation; the
  path-string `std::map`/`unordered_set` machinery and 3× vformat per cancel are deleted.
- Kept verbatim: per-worker keep-alive `httplib::Client` map, atomic cache write (tmp + rename),
  direct stb decode, `condition_variable_any` + jthread stop-token shutdown, httplib confined to
  the .cpp. New in options: `connection_timeout_sec` (5) / `read_timeout_sec` (3) — previously
  hardcoded; the tests need short timeouts.

### Manager-side restructuring

- `loading_tiles` (map of 3 futures each) → `unordered_set<tile_key> loading_keys` + a
  `vector<tile_payload> upload_queue`. `loading_tile` and all futures are deleted from tile.hpp.
- Promote each frame: `drain(ready_scratch, dropped_scratch)` → drops erase from `loading_keys`
  (warn-log real failures, silence cancellations) → payloads append to `upload_queue` → budgeted
  upload loop pops from the queue (undesired payloads are dropped; raii frees them). Payloads that
  don't fit the budget wait in the queue — replacing the old "ready future stays polled" behavior.
- Cancels are issued **once per desired-set rebuild** (in `process_current_location`: any loading
  key not in the new set), not every frame — the desired set only changes there, so the per-frame
  cancel loop in `pre_process` is deleted.
- The tile-center doubles (`(key + 0.5) * size`) are computed at promote time from the key — the
  old `loading_tile.tx/tz` carried them through the future for no reason.
- "Loading finished" flips when `loading_keys` *and* `upload_queue` are both empty.
- `loading_count()` has zero callers (grep-verified) — deleted.

## Tests (tests/tile_source_tests.cpp)

`run_tests` now links `raytiles` (raylib arrives transitively; needed for the stb implementation
that lives in raylib's rtextures object). Tests stay windowless — only CPU raylib calls
(`GenImageColor`, `ExportImageToMemory`) to author PNG fixtures. The test TU includes httplib
directly to run a **local** `httplib::Server` on an ephemeral 127.0.0.1 port — all tests offline
and deterministic. To avoid ODR mismatch with the library's httplib (compiled with OpenSSL
support), the test target defines `CPPHTTPLIB_OPENSSL_SUPPORT` and links OpenSSL too.

1. *Cache hit*: pre-seed the three templated paths with valid PNGs → drain yields one payload with
   three decoded images; no server involved (dead host proves it).
2. *HTTP fetch + write-through*: local server serves PNG bytes → payload arrives, cache files
   exist; a second source with a dead host and the same cache dir still succeeds (cache hit).
3. *Corrupt PNG*: garbage bytes in cache → one drop, `cancelled == false`, non-empty reason.
4. *HTTP 404*: server without the route → drop.
5. *Dedup*: two `request`s for one key → exactly one payload total.
6. *Cancel before pickup*: single worker pinned on a slow route (server handler sleeps), second
   request queued then cancelled → drop with `cancelled == true`, and no payload for that key.
   The busy window is seconds vs microseconds of test code — deterministic in practice.

## Risks / mitigations

- Highest-risk step of the plan (threading). Mitigations: the state machine is small (one mutex,
  two queues, one map), every transition erases `in_flight` exactly once, and the test suite drives
  the real worker loop including the cancel window.
- Destructor can still block on an in-flight HTTP fetch (httplib can't abort a blocking Get) —
  pre-existing, documented on the destructor.
- ODR risk between the two httplib TUs — addressed via matching compile definitions (see Tests).

## Re-evaluation (pre-implementation)

- Considered delivering failures and payloads in one variant-typed queue — rejected: two vectors
  and two swaps are simpler than a visit at every drain.
- Considered a `std::string reason` per drop being an allocation on the failure path — accepted:
  failures are rare and worth a log line; cancellations pass an empty string (no allocation).
- Considered keeping the per-frame cancel sweep as a safety net — rejected: desired-set membership
  only changes in `process_current_location`; a sweep elsewhere can never find new work. (The old
  sweep also only "worked" because cancel was a no-op — with real cancels it would re-flag
  already-flagged jobs every frame.)
- Considered `std::deque` for pending — `std::queue` (deque-backed) is what the old code used;
  kept.

## Re-evaluation (post-implementation)

- All 15 test cases green (25 130 assertions): the six new tile_source suites drive the real worker
  pool — cache hit on a dead host, HTTP fetch + cache write-through, corrupt PNG → reasoned drop,
  404 → reasoned drop, in-flight dedup, and cancel-before-pickup (cancelled drop, no payload) —
  fully offline via a local httplib server with a slow route for the deterministic busy window.
- Additions beyond the spec, made during implementation:
  - *Cancelled-but-desired-again re-spawn*: a drop that comes back cancelled while its key is
    desired again (camera returned quickly) is re-requested immediately. Real failures still wait
    for the next desired rebuild — an immediate retry would hammer a failing server. Without this,
    real cancellation (now that it works) would have left holes until the next 500 m rebuild.
  - `tile.hpp` now includes `raytiles/raytiles.h` itself — the header set was silently
    include-order-dependent on the public header (broke the moment tile_source.h included tile.hpp
    standalone). Fixed at the root rather than by ordering includes.
- Deleted along the way: `loading_tile` + all futures, `loading_count()` (zero callers), the
  per-frame cancel sweep, `chrono` from the manager.
- Worker-side raylib rule is now *stricter* than before: failure paths free pixels via
  `stbi_image_free` directly, so not even UnloadImage runs off-main.
- ODR guard verified: run_tests compiles its httplib TU with `CPPHTTPLIB_OPENSSL_SUPPORT` to match
  the library's.
- Suite runtime grew to ~5 s (two deliberate 2 s busy windows). Acceptable; noted in case CI minutes
  ever matter.
