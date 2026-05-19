# raytiles - Python binding

ctypes binding for [raytiles](https://github.com/ziv/raytiles), mirroring the
public C API declared in `include/raytiles/craytiles.h` 1:1.

## Install

```bash
pip install ./bindings/python
```

The binding loads `libraytiles` (`.dylib` / `.so` / `.dll`) at import time.
It looks for the library in this order:

1. `RAYTILES_LIB` environment variable (full path to the shared library).
2. The same directory as the installed `raytiles` package.

Until a wheel with the bundled binary is published, set `RAYTILES_LIB`
explicitly, e.g.:

```bash
export RAYTILES_LIB=/path/to/libraytiles.dylib
```

## Quick start

```python
import ctypes
import raytiles as rt

# libraytiles statically links and re-exports raylib's C symbols, so drive
# the window / draw loop through the same dylib (rt.lib). Mixing in a
# separate raylib install would create two GL contexts and crash.
rl = rt.lib
rl.InitWindow.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
rl.InitWindow(1280, 720, b"raytiles")
rl.SetTargetFPS(60)

streamer = rt.RaytilesStreamer()  # all defaults

camera = rt.Camera3D(
    position=rt.Vector3(0, 5000, 5000),
    target=rt.Vector3(0, 0, 0),
    up=rt.Vector3(0, 1, 0),
    fovy=60.0,
    projection=0,  # CAMERA_PERSPECTIVE
)

while not rl.WindowShouldClose():
    rl.UpdateCamera(ctypes.byref(camera), 1)  # CAMERA_FREE
    streamer.update(camera)
    rl.BeginDrawing()
    rl.ClearBackground(rt.Color(102, 191, 255, 255))
    rl.BeginMode3D(camera)
    streamer.draw(camera)
    rl.EndMode3D()
    rl.EndDrawing()

streamer.destroy()
rl.CloseWindow()
```

See `scripts/minimal_app.py` in the repo root for a runnable example.

## Public API

- Value types: `Vector2`, `Vector3`, `Vector4`, `Color`, `Camera3D`
- Configs: `RaytilesWorldConfig`, `RaytilesStreamingConfig`,
  `RaytilesRenderingConfig`, `RaytilesPoolConfig`
- Defaults: `world_config_default()`, `streaming_config_default()`,
  `rendering_config_default()`, `pool_config_default()`
- Classes: `RaytilesStreamer`, `RaytilesRenderer`
- Library handle: `lib` (raw `ctypes.CDLL` - also hosts raylib's
  re-exported symbols)
