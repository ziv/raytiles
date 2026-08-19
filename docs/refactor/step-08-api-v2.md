# Step 8 — Public API v2: one nested config, raylib types, pImpl

**Status:** DONE
**Commit:** `feat!: nested config, raylib-native types, and a pImpl streamer`

## Shape (final)

```cpp
namespace raytiles {
struct world_config     { int anchor_x_tile, anchor_z_tile; Zoom base_zoom, max_zoom;
                          float tile_size; std::array<float, zoom_levels> skirt_overlap;
                          bool mipmaps; Vector3 origin_offset; };
struct streaming_config { int radius; std::array<float, zoom_levels> thresholds;
                          float update_distance /* meters, squared internally */;
                          double upload_budget_sec; int max_uploads_per_frame;
                          double near_plane, far_plane; };
struct rendering_config { float fog_start, fog_end, skirt_drop;
                          Color fog_color = BLUE; Color ambient_light = WHITE;
                          Vector3 sun_direction; float sun_scale, height_scale, normals_scale; };
struct network_config   { int threads; bool allow_insecure_tls;
                          int connection_timeout_sec, read_timeout_sec;
                          std::string cache_dir = ".cache";
                          std::string texture_url, heightmap_url, normals_url; };
struct config { world_config world; streaming_config streaming;
                rendering_config rendering; network_config network; };

class streamer {
    explicit streamer(config conf = {});
    streamer(double latitude, double longitude, config conf = {});
    void update(const Camera3D&, Vector3 world_offset = {0,0,0});
    void draw();  void draw_debug_3d();  void draw_debug_labels();
    bool is_loading() const;  float loading_progress() const;      // was get_loading
    Vector3 initial_position(float altitude) const;                // was get_initial_position
    std::optional<float> ground_height(Vector3 position) const;    // ±0.5 m grid, bilinear
    void set_rendering(const rendering_config&);                   // bulk push
    void set_fog_color(Color); void set_fog_start(float); void set_fog_end(float);
    void set_ambient_light(Color); void set_sun_direction(Vector3); void set_sun_scale(float);
    void set_height_scale(float); void set_normals_scale(float);
private:
    struct impl; std::unique_ptr<impl> impl_;                      // pImpl
};
}
```

Decisions and their reasons:

- **Sub-structs keep their familiar namespace-scope names** (`world_config` …) and are aggregated
  by `config` — one ctor argument, but docs and C mirrors stay per-topic. The old 4-positional-arg
  ctor is gone.
- **pImpl** rather than moving `Plane`/`Frustum` around: the streamer's privates (frustum, cached
  camera, tuning floats) were the last internals leaking through the public header. `Plane`/
  `Frustum` move to `detail/utils.hpp`. One pointer hop per call — noise against a draw loop.
- **raylib types**: `Color` for fog/ambient (`BLUE`/`WHITE` literals reproduce the old float
  defaults exactly), `Vector3` for sun. 8-bit color channels are a deliberate simplification —
  every real caller already used the `Color` overloads. Float-component and `Vector4` overloads are
  deleted (`ColorNormalize` exists for callers holding floats).
- **`network_config` replaces `pool_config`**: full URL templates stay (`:zoom:/:x:/:y:` tokens),
  URL splitting moves *into* `tile_source` (it owns the HTTP knowledge); the three cache-path
  templates collapse into one `cache_dir` (layout: `cache_dir/{texture,heightmap,normals}/z/x/y.png`
  — same as the default templates and the warm-up script). Custom per-type cache templates are
  dropped — repointing the cache root was the only real use. Timeouts become visible knobs (they
  already existed internally for the tests).
- **Renames**: `rendering_radius`→`radius` (it lives in `streaming` now — no stutter),
  `base_zoom_tile_size`→`tile_size`, `use_mipmap`→`mipmaps`, `offset`→`origin_offset`,
  `update_distance_sq`→`update_distance` (meters), `get_loading`→`loading_progress`,
  `get_initial_position`→`initial_position`.
- **Internals lose their mirror structs**: `tile_store` takes `const config&` (copies world +
  streaming, hands network to its source); `tile_shader` takes `const rendering_config&` directly
  and gains `apply()` for the bulk setter; `tile_store_options`, `tile_source_options` (public
  face), `tile_shader_options`, and all three `make_*_options` translators are deleted.
- **`tiles_renderer` → `terrain_renderer`** (files + class) — completes the plan's naming.
- **C wrapper**: `RaytilesConfig` with four nested mirror structs, `RaytilesConfigDefault()`,
  `RaytilesStreamerCreate(const RaytilesConfig*)` (NULL = defaults) +
  `...CreateLatLon(lat, lon, const RaytilesConfig*)`; getters renamed
  (`...LoadingProgress`, `...InitialPosition`); setter surface trimmed to match C++ 1:1.
  `demo_c.c` updated in the same commit.
- **Sandbox**: all five apps + `utils.h` updated. `sandbox/demo.cpp` carries the author's
  uncommitted edits — they are preserved verbatim; only API call sites change (flagged in the final
  report). `spline.cpp` is not a build target but is updated anyway to keep the directory honest.
- README has no API snippets (verified); the GitHub wiki Quick-Start must be updated by the author
  after release — noted in the close-out doc.

## Tests

`tile_source_tests` adapt to `network_config` (full URLs + `cache_dir`; the custom-template test
dimension disappears with the feature). Everything else is API-stable at the test level.

## Risks / mitigations

- Widest-blast-radius step; all mechanical though. Gate: full build of every target + suite green.
- Bindings mode: pImpl and config are header-level changes; no new raylib link-time symbols.

## Re-evaluation (pre-implementation)

- Considered exposing `render_items()` publicly (the flat list is a nice advanced API) — deferred:
  not in scope, easy to add later without breaking anything.
- Considered keeping `pool_config` as a deprecated alias — rejected: the author chose a full break.
- Considered `struct config` nested-type names (`config::world_t`) — rejected for the flatter
  familiar names; C mirrors read better too.

## Re-evaluation (post-implementation)

- All targets build warning-free (regular *and* bindings mode — verified with a scratch
  `-DRAYTILES_BINDINGS_MODE=ON` configure+build); all 17 test cases / 25 143 assertions green.
- The mirror-struct layer is fully gone: grep confirms `tile_store_options` / `tile_source_options`
  / `tile_shader_options` / `pool_config` no longer exist; `tile_source` resolves URLs + cache
  templates itself; `tile_shader` consumes `rendering_config` directly (colors normalized at the
  GPU boundary via `ColorNormalize`).
- The streamer's setter relays collapsed: `terrain_renderer` exposes its `tile_shader&` and the
  streamer forwards straight to it — the parameter plumbing is one layer, not three.
- `sandbox/demo.cpp`: the author's uncommitted edits are preserved; only the config/`streamer`
  construction, `initial_position`, and `rlSetClipPlanes` source changed. Included in this commit
  out of necessity (the old API no longer compiles) — flagged in the session report.
- `spline.cpp` (not a build target) updated by hand where the sed missed `tile_size`.
- Deviation from spec: none of substance. `MetersSq`/`MetersD`/`MetersDSq` aliases stay in the
  public header (detail code uses them; harmless).
- Remaining for the author: update the GitHub wiki Quick-Start after release (README has no
  snippets, verified).
