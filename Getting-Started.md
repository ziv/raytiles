# Getting Started with Raytiles

Raytiles is a 3D world streaming engine for [raylib](https://www.raylib.com/). It streams satellite imagery and
elevation data as textured mesh tiles around a moving camera, giving you a real-time, bird's-eye view of any location
on Earth.

---

## Prerequisites

| Requirement | Version |
|---|---|
| C++ compiler | C++23 (GCC 13+, Clang 17+, MSVC 2022+) |
| CMake | 3.31+ |
| Ninja (recommended) | any recent version |
| OpenSSL | any recent version (non-Emscripten builds only) |

> **Emscripten builds** use the browser's `Fetch` API instead of OpenSSL + cpp-httplib, so OpenSSL is not required for
> web targets.

All other dependencies (raylib 6.0, cpp-httplib) are automatically fetched and built by CMake via `FetchContent`.

---

## Installation

### CMake FetchContent (recommended)

Add the following to your `CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(
    raytiles
    GIT_REPOSITORY https://github.com/ziv/raytiles.git
    GIT_TAG v0.1.0   # replace with the latest release tag
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raytiles)
```

Then link against the `raytiles` target:

```cmake
target_link_libraries(MyApp PRIVATE raytiles)
```

Because raytiles publicly links raylib, your app automatically inherits the raylib headers and library — no extra
`target_link_libraries` call for raylib is needed.

---

## Quick Start

### C++ API

Include `raytiles.h` and `raylib.h`. A raylib window **must** be open before constructing a `raytiles::streamer`
because shaders and GPU textures require an active OpenGL context.

```cpp
#include "raylib.h"
#include "raytiles.h"

int main() {
    InitWindow(1280, 720, "My Raytiles App");

    // --- Configure the streamer ---
    raytiles::config conf;
    conf.base_zoom          = 11;     // lowest LOD zoom
    conf.max_zoom           = 14;     // highest LOD zoom
    conf.rendering_radius   = 7;      // disc radius in base-zoom tiles
    conf.anchor_x_tile      = 1223;   // X tile coordinate of the world origin
    conf.anchor_z_tile      = 828;    // Z tile coordinate of the world origin

    // --- Configure the download pool ---
    raytiles::pool_config pool_conf;
    pool_conf.download_threads = 2;   // background HTTP workers

    // --- Create the streamer (requires an open window) ---
    raytiles::streamer streamer(conf, pool_conf);

    // --- Set up a camera ---
    Camera3D camera = {};
    camera.position   = { 3000.0f, 5000.0f, 3000.0f };
    camera.target     = { 0.0f, 0.0f, 0.0f };
    camera.up         = { 0.0f, 1.0f, 0.0f };
    camera.fovy       = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Match clip planes to the streaming radius for best depth precision
    rlSetClipPlanes(1, 100000);

    streamer.set_fog_color(SKYBLUE);

    while (!WindowShouldClose()) {
        // 1. Update tile working set and promote finished downloads to the GPU
        streamer.update(camera);

        BeginDrawing();
            ClearBackground(SKYBLUE);
            BeginMode3D(camera);
                // 2. Render the loaded tiles
                streamer.draw(camera);
            EndMode3D();
            // Optional: show streaming statistics HUD
            streamer.debug(camera);
        EndDrawing();
    }

    CloseWindow();
}
```

See [`sandbox/main.cpp`](sandbox/main.cpp) for a full runnable example including keyboard input handling.

---

### C API

Include `craytiles.h` instead when working in plain C or when a C-compatible ABI is required.

```c
#include "raylib.h"
#include "craytiles.h"

int main(void) {
    InitWindow(1280, 720, "My Raytiles App (C)");

    RaytilesConfig conf = RaytilesConfigDefault();
    conf.anchor_x_tile = 1223;
    conf.anchor_z_tile = 828;

    RaytilesPoolConfig pool_conf = RaytilesPoolConfigDefault();
    pool_conf.download_threads = 2;

    RaytilesStreamer *streamer = RaytilesStreamerCreate(&conf, &pool_conf);

    Camera3D camera = {
        .position   = { 3000.0f, 5000.0f, 3000.0f },
        .target     = { 0.0f, 0.0f, 0.0f },
        .up         = { 0.0f, 1.0f, 0.0f },
        .fovy       = 45.0f,
        .projection = CAMERA_PERSPECTIVE,
    };

    while (!WindowShouldClose()) {
        RaytilesStreamerUpdate(streamer, camera);

        BeginDrawing();
            ClearBackground(SKYBLUE);
            BeginMode3D(camera);
                RaytilesStreamerDraw(streamer, camera);
            EndMode3D();
            RaytilesStreamerDebug(streamer, camera);
        EndDrawing();
    }

    RaytilesStreamerDestroy(streamer);
    CloseWindow();
    return 0;
}
```

---

## Tile Providers

Raytiles separates **texture tiles** (satellite imagery) from **heightmap tiles** (elevation data). You can mix
providers freely via `pool_config`.

### Default Providers (no token required)

| Role | Provider | Host | URL template |
|---|---|---|---|
| Texture | Esri World Imagery | `https://server.arcgisonline.com` | `/ArcGIS/rest/services/World_Imagery/MapServer/tile/{zoom}/{y}/{x}` |
| Heightmap | Terrarium / AWS | `https://s3.amazonaws.com` | `/elevation-tiles-prod/terrarium/{zoom}/{x}/{y}.png` |

These defaults are ready to use out of the box.

### Mapbox (token required, supports zoom up to 15)

Sign up at [mapbox.com](https://www.mapbox.com/) to obtain a free access token.

```cpp
pool_conf.texture_host     = "https://api.mapbox.com";
pool_conf.texture_url_path = "/v4/mapbox.satellite/{zoom}/{x}/{y}.png?access_token=<YOUR_TOKEN>";

pool_conf.heightmap_host     = "https://api.mapbox.com";
pool_conf.heightmap_url_path = "/v4/mapbox.terrain-rgb/{zoom}/{x}/{y}.pngraw?access_token=<YOUR_TOKEN>";
```

Mapbox uses a different height encoding formula (`-10000 + (R*65536 + G*256 + B) * 0.1`) which is built into
the library.

### Zoom Level Limit

The Mapbox heightmap API provides elevation data up to zoom level 15.  Zoom levels above 15 are internally
upscaled from the zoom-15 tile (bilinear approximation), while higher-zoom texture tiles are fetched directly.

---

## Configuration Reference

### `raytiles::config`

| Field | Default | Description |
|---|---|---|
| `base_zoom` | `11` | Lowest LOD zoom; tiles far from the camera stay at this level. |
| `max_zoom` | `14` | Highest LOD zoom; tiles near the camera are subdivided to this level. |
| `base_zoom_tile_size` | `16600.0f` | World size (meters) of one tile at `base_zoom`. |
| `rendering_radius` | `7` | Radius of loaded tiles around the camera (in `base_zoom` tile units). |
| `skirt_size` | `15.0f` | Overlap factor used to hide cracks between neighboring tiles at different LODs. |
| `height_scale` | `1.0f` | Multiplier applied to heightmap values (increase for dramatic terrain). |
| `update_distance` | `1000²` | Squared XZ distance the camera must move before the tile set is recomputed. |
| `update_height` | `500.0f` | Altitude delta (meters) that triggers a tile-set recomputation. |
| `upload_budget_sec` | `0.002` | Wall-clock budget per frame (seconds) for uploading decoded tiles to the GPU. |
| `max_uploads_per_frame` | `8` | Hard cap on tile promotions per frame. |
| `anchor_x_tile` | `1223` | X tile index at `base_zoom` that maps to world X = 0. |
| `anchor_z_tile` | `828` | Z tile index at `base_zoom` that maps to world Z = 0. |
| `near_plane` | `1` | Near clip plane (meters). |
| `far_plane` | `100000` | Far clip plane (meters). |
| `use_mipmap` | `true` | Generate trilinear/anisotropic mipmaps on texture upload. |
| `skirt_drop` | `1000.0f` | Depth (meters) of skirt geometry below each tile edge. |
| `fog_start` | `40000.0f` | Distance (meters) at which fog begins. |
| `fog_end` | `70000.0f` | Distance (meters) at which fog is fully opaque. |
| `use_logger` | `false` | Enable raylib `TraceLog` output from the main thread. |

### `raytiles::pool_config`

| Field | Default | Description |
|---|---|---|
| `download_threads` | `2` | Number of background HTTP download workers. |
| `allow_insecure_tls` | `false` | Skip TLS certificate verification (only for local proxies). |
| `use_logger` | `false` | Enable logging from pool worker threads. |
| `texture_cache_path` | `"assets/tiles/texture/{}/{}/{}.png"` | On-disk cache path for texture tiles. |
| `heightmap_cache_path` | `"assets/tiles/heightmap/{}/{}/{}.png"` | On-disk cache path for heightmap tiles. |
| `texture_host` | `"https://server.arcgisonline.com"` | Base URL for texture tile downloads. |
| `texture_url_path` | Esri template | URL path template for texture tiles (`{zoom}`, `{x}`, `{y}`). |
| `heightmap_host` | `"https://s3.amazonaws.com"` | Base URL for heightmap tile downloads. |
| `heightmap_url_path` | Terrarium template | URL path template for heightmap tiles. |

---

## Setting the World Origin (Anchor)

The `anchor_x_tile` and `anchor_z_tile` fields in `config` define which tile is placed at the world origin
(position 0, 0, 0). Use a tile calculator (e.g. the
[Slippy Map Tile Numbers](https://wiki.openstreetmap.org/wiki/Slippy_map_tilenames) article on the OSM wiki) to
find the tile coordinates for your area of interest at `base_zoom`.

```cpp
// Example: center the world over Athens, Greece (zoom 11)
conf.anchor_x_tile = 1179;
conf.anchor_z_tile = 797;
```

---

## Terrain Altitude Queries

`streamer::ground_height` returns the terrain altitude (world Y) directly under any XZ position by reading the
cached heightmap pixel — an O(1) operation.

```cpp
auto y = streamer.ground_height(camera.position);
if (y.has_value()) {
    // use *y for collision detection, spawning, etc.
}
```

Returns `std::nullopt` when no loaded tile covers the queried point; fall back to the previous frame's value or 0.

---

## Tile Cache

Downloaded tiles are persisted to disk and reused on subsequent runs. The default layout is:

```
assets/tiles/texture/{zoom}/{x}/{z}.png
assets/tiles/heightmap/{zoom}/{x}/{z}.png
```

Override the paths via `pool_config::texture_cache_path` and `pool_config::heightmap_cache_path`. Parent
directories are created automatically.

On Emscripten (browser) builds the cache is stored in IndexedDB via the IDBFS filesystem, using paths such as
`/assets/t/{}/{}/{}.png`.

---

## Building from Source

Clone the repository and build with CMake + Ninja:

```bash
git clone https://github.com/ziv/raytiles.git
cd raytiles

# Configure
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build (produces libraytiles.a and the Sandbox example)
cmake --build build
```

For a debug build:

```bash
cmake -B cmake-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug
```

### Platform Notes

| Platform | Notes |
|---|---|
| Linux | Requires `libssl-dev` (OpenSSL). Install via your package manager, e.g. `apt install libssl-dev`. |
| macOS | OpenSSL can be installed via Homebrew: `brew install openssl`. The `Security.framework` is linked automatically. |
| Windows | Provide an OpenSSL installation and set `OPENSSL_ROOT_DIR` if CMake cannot find it automatically. |
| Emscripten | Run `emcmake cmake -B build-em -G Ninja` and then `cmake --build build-em`. The output is a self-contained `.html` file. |

### Running the Sandbox

The `Sandbox` executable (located in your build directory) is a minimal demo that streams Greece's islands:

```bash
./build/Sandbox
```

| Key | Action |
|---|---|
| `W` / `S` | Move camera forward / backward |
| `A` / `D` | Move camera left / right |

---

## Architecture Overview

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

Each frame the streamer runs three phases:

1. **Decide** — compute which tiles should be visible at the current camera position and LOD.
2. **Promote** — decode finished downloads and upload them to GPU within the configured time/count budget.
3. **Render** — issue draw calls for all currently loaded tiles.

Background workers are I/O-only; they never call into raylib (which is not thread-safe). All decoding and GPU
uploads happen on the main thread.

---

## Further Reading

- [`raytiles.h`](raytiles.h) — full C++ API with inline documentation.
- [`craytiles.h`](craytiles.h) — C wrapper API.
- [`docs.md`](docs.md) — provider URLs, tile budget notes, and the zoom-15 limitation.
- [`sandbox/main.cpp`](sandbox/main.cpp) — full runnable example.
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — how to contribute.
