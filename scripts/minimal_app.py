"""Minimal raytiles app, all-defaults.

`libraytiles.dylib` statically links its own raylib, so the GL state lives
inside that dylib. We therefore call raylib through the same dylib (via the
`raytiles._lib` ctypes handle) rather than via a separate `pyray` install -
otherwise there would be two raylib instances and libraytiles would crash
on first draw because *its* InitWindow was never called.

Run:
    python scripts/minimal_app.py
"""

import ctypes
import os
import sys

# Make the binding importable without installing it, and tell it where the
# dylib lives (still in scripts/ for now).
_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO = os.path.dirname(_HERE)
sys.path.insert(0, os.path.join(_REPO, "bindings", "python"))
os.environ.setdefault("RAYTILES_LIB", os.path.join(_HERE, "libraytiles.dylib"))

import raytiles as rt

WIDTH, HEIGHT = 800, 600

# raylib enum constants (from raylib_api.xml, stable across 5.x)
FLAG_MSAA_4X_HINT = 32
CAMERA_PERSPECTIVE = 0
CAMERA_FREE = 1

# Colors (RGBA)
SKYBLUE = rt.Color(102, 191, 255, 255)
RAYWHITE = rt.Color(245, 245, 245, 255)

# ---------------------------------------------------------------------------
#  raylib bindings via the same dylib that hosts raytiles
# ---------------------------------------------------------------------------
_rl = rt.lib  # same dylib -> same raylib instance as the streamer


def _bind(name, restype, argtypes):
    fn = getattr(_rl, name)
    fn.restype = restype
    fn.argtypes = argtypes
    return fn


_SetConfigFlags = _bind("SetConfigFlags", None, [ctypes.c_uint])
_InitWindow = _bind("InitWindow", None, [ctypes.c_int, ctypes.c_int, ctypes.c_char_p])
_CloseWindow = _bind("CloseWindow", None, [])
_WindowShouldClose = _bind("WindowShouldClose", ctypes.c_bool, [])
_SetTargetFPS = _bind("SetTargetFPS", None, [ctypes.c_int])
_BeginDrawing = _bind("BeginDrawing", None, [])
_EndDrawing = _bind("EndDrawing", None, [])
_ClearBackground = _bind("ClearBackground", None, [rt.Color])
_BeginMode3D = _bind("BeginMode3D", None, [rt.Camera3D])
_EndMode3D = _bind("EndMode3D", None, [])
_UpdateCamera = _bind("UpdateCamera", None, [ctypes.POINTER(rt.Camera3D), ctypes.c_int])
_DrawFPS = _bind("DrawFPS", None, [ctypes.c_int, ctypes.c_int])
_IsKeyPressed = _bind("IsKeyPressed", ctypes.c_bool, [ctypes.c_int])
_DrawText = _bind(
    "DrawText",
    None,
    [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, rt.Color],
)


def main() -> None:
    _SetConfigFlags(FLAG_MSAA_4X_HINT)
    _InitWindow(WIDTH, HEIGHT, b"raytiles - minimal python app")
    _SetTargetFPS(60)

    # All-defaults streamer. The GL context above is now live.
    streamer = rt.RaytilesStreamer()

    camera = rt.Camera3D(
        position=rt.Vector3(0.0, 5000.0, 5000.0),
        target=rt.Vector3(0.0, 5000.0, 0.0),
        up=rt.Vector3(0.0, 1.0, 0.0),
        fovy=60.0,
        projection=CAMERA_PERSPECTIVE,
    )

    y = 5000.0;

    try:
        while not _WindowShouldClose():
            # if _IsKeyPressed(rt.KEY_UP):
                # camera.p += 100.0
            _UpdateCamera(ctypes.byref(camera), CAMERA_FREE)
            streamer.update(camera)

            _BeginDrawing()
            _ClearBackground(SKYBLUE)

            _BeginMode3D(camera)
            streamer.draw(camera)
            streamer.debug_3d()
            _EndMode3D()

            streamer.debug(camera)

            if streamer.is_loading():
                pct = int(streamer.get_loading() * 100)
                _DrawText(
                    f"Loading... {pct}%".encode("utf-8"),
                    20, HEIGHT - 40, 20, RAYWHITE,
                )

            _DrawFPS(10, 10)
            _EndDrawing()
    finally:
        streamer.destroy()
        _CloseWindow()


if __name__ == "__main__":
    main()
