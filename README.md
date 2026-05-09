# Raytiles

<img src="res/logo.png" alt="logo" width="256" align="right"/>

3D world streaming engine 🌎 for [raylib](https://www.raylib.com/). Get a bird's-eye view of the world around you,
rendered
in real-time from satellite imagery and elevation data.

Originally developed for a flight simulator project, and extracted into a standalone library to allow embedding in any
raylib application.

Built for small-scale flight simulators, mission planners, and any other geospatial visualizations. Allow streaming any
location on Earth at zoom level up to 15.

![GitHub Release](https://img.shields.io/github/v/release/ziv/raytiles)
![GitHub License](https://img.shields.io/github/license/ziv/raytiles)

[![macOS Build](https://github.com/ziv/raytiles/actions/workflows/macos.yml/badge.svg)](https://github.com/ziv/raytiles/actions/workflows/macos.yml)
[![Linux Build](https://github.com/ziv/raytiles/actions/workflows/linux.yml/badge.svg)](https://github.com/ziv/raytiles/actions/workflows/linux.yml)
[![Windows Build](https://github.com/ziv/raytiles/actions/workflows/windows.yml/badge.svg)](https://github.com/ziv/raytiles/actions/workflows/windows.yml)
[![Emscripten Build](https://github.com/ziv/raytiles/actions/workflows/emscripten.yml/badge.svg)](https://github.com/ziv/raytiles/actions/workflows/emscripten.yml)

## Features

- Streaming **ANY** location on Earth!
- **Background** tile downloading (HTTP + persistent on-disk cache).
- Adaptive **LOD** (level-of-detail): more detail near the camera, less far away.
- **Lights and shadows** via normal maps.
- **GPU**-side displacement via a heightmap-driven vertex shader.
- Per-frame upload **budgeting**, no GPU stalls on bursty load.
- Ground-truth **altitude queries** (`ground_height`) for collision / spawning.
- **RAII** everywhere: zero manual `Unload*` calls, zero leaks on error paths.
- Pure **C++** and **C** wrapper APIs (`raytiles.h` and `craytiles.h`).
- **Cross-platform** builds for Windows, Linux, and macOS.
- **Configurable**! fit it to your needs by tweaking `raytiles::config` fields.
- **Open-source** and permissively licensed (MIT)

## Examples

The following example video is part of the islands of Greece, rendered with Mapbox tiles at zoom level 11 to 14:

https://github.com/user-attachments/assets/0422ffea-654f-4299-8860-23f99d7d98ec

This example demonstate lights and shadows:

https://github.com/user-attachments/assets/6e373cb4-a1fa-4c21-a72a-db2d0bd96a89


## How It Works

![how it works](res/how-it-works0.png)

## Quick Start

If you are using CMake, you can add **raytiles** using `FetchContent`:

```cmake
include(FetchContent)
FetchContent_Declare(
        raytiles
        GIT_REPOSITORY https://github.com/ziv/raytiles.git
        GIT_TAG v0.1.0
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raytiles)
```

The following example shows how to use the C++ API to stream and render the world in a raylib application:

```cpp
#include "raylib.h"
#include "raytiles.h"

int main() {
  InitWindow(1280, 720, "raytiles");

  // streamer confuguration with default values, tweak as needed
  raytiles::config conf;
  
  // pool configuration with default values, tweak as needed
  raytiles::pool_config pool_conf;

  // streamer instance
  raytiles::streamer streamer(conf, pool_conf);

  Camera3D camera = /* ... your camera ... */;

  while (!WindowShouldClose()) {
    // update the streamer with the current camera state
    streamer.update(camera);

    BeginDrawing();
        ClearBackground(SKYBLUE);
        BeginMode3D(camera);
            // draw the streamed world
            streamer.draw(camera);
        EndMode3D();
    EndDrawing();
  }

  CloseWindow();
}
```

See `sandbox/main.cpp` for a full runnable example with input handling.

## Providers

**raytiles** is designed to be provider-agnostic, allowing you to use any tile server that follows the XYZ tiling
scheme. By default, it comes with a built-in providers, Esri for texture and Terrarium for heightmap, but you can easily
configure any other provider using the `raytiles::pool_config ` struct.

The default providers are configured as follows:

```c++
raytiles::pool_config pool_conf;

pool_conf.texture_host          = "https://server.arcgisonline.com";
pool_conf.texture_url_path      = "/ArcGIS/rest/services/World_Imagery/MapServer/tile/{zoom}/{y}/{x}";

pool_conf.heightmap_host        = "https://s3.amazonaws.com";
pool_conf.heightmap_url_path    = "/elevation-tiles-prod/terrarium/{zoom}/{x}/{y}.png";
```

Replacing the heightmap provider require to choose the right height calculation strategy. [TBC]

---

## TODOs

- [x] support normals for lighting (in progress)
- [ ] support more than level 15 zoom
- [ ] add sky module (atmosphere + sun + moon + stars)

---

<img src="res/raylove.png" align="left">

Made with ❤️ to the raylib community. If you find this library useful, please consider starring the repo and sharing it
with your friends! Contributions and feedback are always welcome. Happy coding!
