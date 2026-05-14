# Raytiles

## Providers APIs

Currently, default is **Terrarium** and **Esri**, but the design allows for any provider that serves heightmaps and
textures
via a URL template. The URL template should have three placeholders for zoom, x, and y (or z) in any order.

### Mapbox (z/x/y)

Require token. Provide both textures and heightmaps.

```c++
std::string texture_url_path = "/v4/mapbox.satellite/{}/{}/{}.png?access_token=***";
std::string heightmap_url_path = "/v4/mapbox.terrain-rgb/{}/{}/{}.pngraw?access_token=***";
```

Mapbox encoding:

```c++
float heightValue = -10000.0 + ((c.r * 65536.0 + c.g * 256.0 + c.b) * 0.1);
```

## Heightmap Terrarium (z/x/y)

```c++
std::string heightmap_host = "https://s3.amazonaws.com";
std::string heightmap_url_path = "/elevation-tiles-prod/terrarium/{}/{}/{}.png";
```

Terrarium encoding:

```c++
float heightValue = ((c.r * 256.0 + c.g + c.b / 256.0) - 32768.0) * 0.1;
```

## Textures Esri (z/y/x)

```c++
std::string texture_host = "https://server.arcgisonline.com";
std::string texture_url_path = "/ArcGIS/rest/services/World_Imagery/MapServer/tile/{}/{}/{}";
```

## The zoom 15 limit

The Mapbox heightmap API only provides data up to zoom level 15, which means that the maximum LOD is 15.
The Mapbox texture API provides data up to zoom level 22.

We need to be able to render higher LODs than 15, so the heightmaps for zoom levels 16-22 are generated on the fly by
upscaling the zoom 15 heightmap.

This is a common technique called "mipmapping" or "level of detail (LOD) generation".
The upscaled heightmaps won't have the same level of detail as the original zoom 15 heightmap, but they will still
provide a reasonable approximation for rendering purposes. The textures for zoom levels 16-22 are fetched directly from
the Mapbox texture API, so they will have the same level of detail as the original zoom 15 textures.

### Implementation plan proposal

- [ ] when zoom > 15 and cache not exists, fetch the zoom 15 heightmap and upscale it to the desired zoom level using a
  simple algorithm (e.g. bilinear interpolation).
- [ ] cache the generated heightmaps on disk to avoid regenerating them every time.
- [ ] the rest should be the same.

## The tiles budget

We want to keep the number of loaded tiles under control to avoid running out of memory or overwhelming the GPU.
The number of tiles should be constant and the change is the LOD, not the radius. The radius should be large enough to
cover the screen at all times, but not too large to cause excessive loading times or memory usage.

All images are 256x256 pixels, so a single tile is 256x256x4 bytes (RGBA8) = 256 KB.

As rule of thumb, we should have up to 512 tiles loaded into memory (all LODs combined) at any given time and about half
is actually rendered (frustum culling).

## The clip planes

Should reflect the distance of sight and the LODs. Ideally, should be proportional to the camera's y position.

Should allow fog to blend in smoothly and hide the pop-in of new tiles. The far clip plane should be at least as far as
the maximum LOD radius.

## Architecture

```
                     ┌──────────────┐
   camera position → │   streamer   │ → draw calls (raylib)
                     └──────┬───────┘
                            │
                ┌───────────┴───────────┐
                │                       │
        ┌───────▼─────┐         ┌───────▼──────┐
        │ desired set │         │ download pool│ ── HTTP ──▶ map provider
        │  (per-frame │         │  (N workers, │       │
        │    LOD)     │         │   bytes only)│       ▼
        └───────┬─────┘         └───────┬──────┘   on-disk
                │                       │           cache
                │       futures<bytes>  │
                │   ┌───────────────────┘
                ▼   ▼
        ┌──────────────────┐                 ┌──────────────────┐
        │  loading tiles   │ ── decode ────▶ │ rendering tiles  │
        │  (futures)       │   + upload      │ (GPU textures)   │
        └──────────────────┘   on main       └──────────────────┘
```

The streamer runs three logical phases each frame:

1. **Decide** — what tiles should be visible at the current camera position?
2. **Promote** — pick up any finished downloads, decode + upload to the GPU within a wall-clock budget.
3. **Render** — draw everything currently loaded.

Workers never touch raylib (it isn't thread-safe). Decoding and GPU uploads happen on the main thread; workers only
move bytes from the network onto disk and into a `std::shared_future<std::string>`.

## Public API

Everything you need is in [`include/raytiles.h`](include/raytiles.h):

- **`raytiles::config`** — tunables (zoom range, render radius, fog, cache paths, …). All defaults are sensible.
- **`raytiles::provider`** — wraps your map-service API token; constructs URLs.
- **`raytiles::streamer`** — the per-frame driver: `update`, `draw`, `debug`, `set_ambient_light`, `set_fog_color`,
  `ground_height`.

Each field and method is documented inline; build with Doxygen or read the header directly.

## Building

CMake + Ninja, C++23. raylib is fetched and built automatically via `FetchContent`:

```bash
cmake -B build -G Ninja
cmake --build build
```

Outputs `libraytiles.a` plus the `Sandbox` example.

### Required environment

- A Mapbox token (or compatible provider) is needed at runtime; the sandbox reads `MAPBOX_TOKEN` from the environment.

## Tile Cache

Tiles are downloaded once and cached forever under `assets/tiles/{texture,heightmap}/{zoom}/{x}/{z}.png`. The cache
is the whole point of background streaming: even tiles you fly over briefly stay on disk and load instantly next time.
Override the layout via `config::texture_cache_path` and `config::heightmap_cache_path`.
