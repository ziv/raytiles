"""Python binding for the raytiles C wrapper (see include/raytiles/craytiles.h).

Mirrors the C API 1:1: configuration structs, default-initializers, a
`RaytilesStreamer` class wrapping the opaque streamer handle, and a
`RaytilesRenderer` view onto its renderer.
"""

import ctypes
import os
import sys
from typing import Optional


# ---------------------------------------------------------------------------
#  Library loading
# ---------------------------------------------------------------------------

def _default_lib_name() -> str:
    if sys.platform == "darwin":
        return "libraytiles.dylib"
    if sys.platform == "win32":
        return "raytiles.dll"
    return "libraytiles.so"


_lib_path = os.environ.get(
    "RAYTILES_LIB",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), _default_lib_name()),
)
_lib = ctypes.CDLL(_lib_path)

#: Public handle to the loaded ``libraytiles`` dynamic library.
#:
#: This is a :class:`ctypes.CDLL` object - the same one used internally to
#: bind every ``Raytiles*`` function. It is exposed publicly because
#: ``libraytiles`` statically links its own copy of raylib and re-exports
#: raylib's C symbols (``InitWindow``, ``BeginDrawing``, ``UpdateCamera``,
#: ...). Callers who want a window / draw loop should bind those symbols
#: through ``lib`` rather than installing a separate raylib package -
#: otherwise the process ends up with two raylib instances and the streamer
#: crashes because *its* GL context was never initialized.
lib = _lib


# ---------------------------------------------------------------------------
#  raylib value types (passed by value in the C ABI)
# ---------------------------------------------------------------------------

class Vector2(ctypes.Structure):
    _fields_ = [("x", ctypes.c_float), ("y", ctypes.c_float)]


class Vector3(ctypes.Structure):
    _fields_ = [
        ("x", ctypes.c_float),
        ("y", ctypes.c_float),
        ("z", ctypes.c_float),
    ]


class Vector4(ctypes.Structure):
    _fields_ = [
        ("x", ctypes.c_float),
        ("y", ctypes.c_float),
        ("z", ctypes.c_float),
        ("w", ctypes.c_float),
    ]


class Color(ctypes.Structure):
    _fields_ = [
        ("r", ctypes.c_ubyte),
        ("g", ctypes.c_ubyte),
        ("b", ctypes.c_ubyte),
        ("a", ctypes.c_ubyte),
    ]


class Camera3D(ctypes.Structure):
    _fields_ = [
        ("position", Vector3),
        ("target", Vector3),
        ("up", Vector3),
        ("fovy", ctypes.c_float),
        ("projection", ctypes.c_int),
    ]


# ---------------------------------------------------------------------------
#  Configuration structs
# ---------------------------------------------------------------------------

class RaytilesWorldConfig(ctypes.Structure):
    _fields_ = [
        ("anchor_x_tile", ctypes.c_int),
        ("anchor_z_tile", ctypes.c_int),
        ("base_zoom", ctypes.c_int),
        ("max_zoom", ctypes.c_int),
        ("base_zoom_tile_size", ctypes.c_float),
        ("skirt_overlap_zooms", ctypes.POINTER(ctypes.c_int)),
        ("skirt_overlap_values", ctypes.POINTER(ctypes.c_float)),
        ("skirt_overlap_count", ctypes.c_int),
        ("use_mipmap", ctypes.c_bool),
        ("use_logger", ctypes.c_bool),
    ]


class RaytilesStreamingConfig(ctypes.Structure):
    _fields_ = [
        ("rendering_radius", ctypes.c_int),
        ("threshold_zooms", ctypes.POINTER(ctypes.c_int)),
        ("threshold_values", ctypes.POINTER(ctypes.c_float)),
        ("thresholds_count", ctypes.c_int),
        ("update_distance_sq", ctypes.c_double),
        ("update_height", ctypes.c_float),
        ("upload_budget_sec", ctypes.c_double),
        ("max_uploads_per_frame", ctypes.c_int),
        ("near_plane", ctypes.c_double),
        ("far_plane", ctypes.c_double),
    ]


class RaytilesRenderingConfig(ctypes.Structure):
    _fields_ = [
        ("fog_start", ctypes.c_float),
        ("fog_end", ctypes.c_float),
        ("skirt_drop", ctypes.c_float),
        ("fog_color", ctypes.c_float * 4),
        ("ambient_light", ctypes.c_float * 4),
        ("sun_direction", ctypes.c_float * 3),
        ("sun_scale", ctypes.c_float),
        ("height_scale", ctypes.c_float),
        ("normals_scale", ctypes.c_float),
    ]


class RaytilesPoolConfig(ctypes.Structure):
    _fields_ = [
        ("download_threads", ctypes.c_int),
        ("allow_insecure_tls", ctypes.c_bool),
        ("use_logger", ctypes.c_bool),
        ("texture_cache_path", ctypes.c_char_p),
        ("heightmap_cache_path", ctypes.c_char_p),
        ("normals_cache_path", ctypes.c_char_p),
        ("texture_url", ctypes.c_char_p),
        ("heightmap_url", ctypes.c_char_p),
        ("normals_url", ctypes.c_char_p),
        ("texture_host", ctypes.c_char_p),
        ("texture_url_path", ctypes.c_char_p),
        ("heightmap_host", ctypes.c_char_p),
        ("heightmap_url_path", ctypes.c_char_p),
        ("normals_host", ctypes.c_char_p),
        ("normals_url_path", ctypes.c_char_p),
    ]


# Opaque handles
class _RaytilesStreamerOpaque(ctypes.Structure):
    pass


class _RaytilesRendererOpaque(ctypes.Structure):
    pass


_StreamerPtr = ctypes.POINTER(_RaytilesStreamerOpaque)
_RendererPtr = ctypes.POINTER(_RaytilesRendererOpaque)


# ---------------------------------------------------------------------------
#  Function signatures
# ---------------------------------------------------------------------------

def _bind(name, restype, argtypes):
    fn = getattr(_lib, name)
    fn.restype = restype
    fn.argtypes = argtypes
    return fn


# Default-initializers
_RaytilesWorldConfigDefault = _bind("RaytilesWorldConfigDefault", RaytilesWorldConfig, [])
_RaytilesStreamingConfigDefault = _bind("RaytilesStreamingConfigDefault", RaytilesStreamingConfig, [])
_RaytilesRenderingConfigDefault = _bind("RaytilesRenderingConfigDefault", RaytilesRenderingConfig, [])
_RaytilesPoolConfigDefault = _bind("RaytilesPoolConfigDefault", RaytilesPoolConfig, [])

# Streamer
_RaytilesStreamerCreate = _bind(
    "RaytilesStreamerCreate",
    _StreamerPtr,
    [
        ctypes.POINTER(RaytilesWorldConfig),
        ctypes.POINTER(RaytilesStreamingConfig),
        ctypes.POINTER(RaytilesRenderingConfig),
        ctypes.POINTER(RaytilesPoolConfig),
    ],
)
_RaytilesStreamerDestroy = _bind("RaytilesStreamerDestroy", None, [_StreamerPtr])
_RaytilesStreamerUpdate = _bind("RaytilesStreamerUpdate", None, [_StreamerPtr, Camera3D])
_RaytilesStreamerDraw = _bind("RaytilesStreamerDraw", None, [_StreamerPtr, Camera3D])
_RaytilesStreamerDebug = _bind("RaytilesStreamerDebug", None, [_StreamerPtr, Camera3D])
_RaytilesStreamerDebug3D = _bind("RaytilesStreamerDebug3D", None, [_StreamerPtr])
_RaytilesStreamerGetRenderer = _bind("RaytilesStreamerGetRenderer", _RendererPtr, [_StreamerPtr])
_RaytilesStreamerIsLoading = _bind("RaytilesStreamerIsLoading", ctypes.c_bool, [_StreamerPtr])
_RaytilesStreamerGetLoading = _bind("RaytilesStreamerGetLoading", ctypes.c_float, [_StreamerPtr])
_RaytilesStreamerGroundHeight = _bind(
    "RaytilesStreamerGroundHeight",
    ctypes.c_bool,
    [_StreamerPtr, Vector3, ctypes.POINTER(ctypes.c_float)],
)

# Renderer
_RaytilesRendererSetAmbientLight = _bind("RaytilesRendererSetAmbientLight", None, [_RendererPtr, Color])
_RaytilesRendererSetAmbientLightV4 = _bind("RaytilesRendererSetAmbientLightV4", None, [_RendererPtr, Vector4])
_RaytilesRendererSetAmbientLightRGBA = _bind(
    "RaytilesRendererSetAmbientLightRGBA",
    None,
    [_RendererPtr, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float],
)
_RaytilesRendererSetFogColor = _bind("RaytilesRendererSetFogColor", None, [_RendererPtr, Color])
_RaytilesRendererSetFogColorV4 = _bind("RaytilesRendererSetFogColorV4", None, [_RendererPtr, Vector4])
_RaytilesRendererSetFogColorRGBA = _bind(
    "RaytilesRendererSetFogColorRGBA",
    None,
    [_RendererPtr, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float],
)
_RaytilesRendererSetFogStart = _bind("RaytilesRendererSetFogStart", None, [_RendererPtr, ctypes.c_float])
_RaytilesRendererSetFogEnd = _bind("RaytilesRendererSetFogEnd", None, [_RendererPtr, ctypes.c_float])
_RaytilesRendererSetHeightScale = _bind("RaytilesRendererSetHeightScale", None, [_RendererPtr, ctypes.c_float])
_RaytilesRendererSetNormalsScale = _bind("RaytilesRendererSetNormalsScale", None, [_RendererPtr, ctypes.c_float])
_RaytilesRendererSetSunDirection = _bind("RaytilesRendererSetSunDirection", None, [_RendererPtr, Vector3])
_RaytilesRendererSetSunScale = _bind("RaytilesRendererSetSunScale", None, [_RendererPtr, ctypes.c_float])


# ---------------------------------------------------------------------------
#  Default-initializer helpers
# ---------------------------------------------------------------------------

def world_config_default() -> RaytilesWorldConfig:
    return _RaytilesWorldConfigDefault()


def streaming_config_default() -> RaytilesStreamingConfig:
    return _RaytilesStreamingConfigDefault()


def rendering_config_default() -> RaytilesRenderingConfig:
    return _RaytilesRenderingConfigDefault()


def pool_config_default() -> RaytilesPoolConfig:
    return _RaytilesPoolConfigDefault()


# ---------------------------------------------------------------------------
#  Renderer wrapper (non-owning view)
# ---------------------------------------------------------------------------

class RaytilesRenderer:
    """Non-owning view onto a streamer's renderer. Do not construct directly;
    obtain it via `RaytilesStreamer.get_renderer()`. Lifetime is tied to the
    owning streamer.
    """

    def __init__(self, handle: _RendererPtr):
        self._handle = handle

    def _check(self):
        if not self._handle:
            raise RuntimeError("RaytilesRenderer handle is null")

    # Ambient light --------------------------------------------------------
    def set_ambient_light(self, color: Color) -> None:
        self._check()
        _RaytilesRendererSetAmbientLight(self._handle, color)

    def set_ambient_light_v4(self, color: Vector4) -> None:
        self._check()
        _RaytilesRendererSetAmbientLightV4(self._handle, color)

    def set_ambient_light_rgba(self, r: float, g: float, b: float, a: float) -> None:
        self._check()
        _RaytilesRendererSetAmbientLightRGBA(self._handle, r, g, b, a)

    # Fog color ------------------------------------------------------------
    def set_fog_color(self, color: Color) -> None:
        self._check()
        _RaytilesRendererSetFogColor(self._handle, color)

    def set_fog_color_v4(self, color: Vector4) -> None:
        self._check()
        _RaytilesRendererSetFogColorV4(self._handle, color)

    def set_fog_color_rgba(self, r: float, g: float, b: float, a: float) -> None:
        self._check()
        _RaytilesRendererSetFogColorRGBA(self._handle, r, g, b, a)

    # Fog distances --------------------------------------------------------
    def set_fog_start(self, distance: float) -> None:
        self._check()
        _RaytilesRendererSetFogStart(self._handle, distance)

    def set_fog_end(self, distance: float) -> None:
        self._check()
        _RaytilesRendererSetFogEnd(self._handle, distance)

    # Shader scalars -------------------------------------------------------
    def set_height_scale(self, scale: float) -> None:
        self._check()
        _RaytilesRendererSetHeightScale(self._handle, scale)

    def set_normals_scale(self, scale: float) -> None:
        self._check()
        _RaytilesRendererSetNormalsScale(self._handle, scale)

    def set_sun_direction(self, direction: Vector3) -> None:
        self._check()
        _RaytilesRendererSetSunDirection(self._handle, direction)

    def set_sun_scale(self, scale: float) -> None:
        self._check()
        _RaytilesRendererSetSunScale(self._handle, scale)


# ---------------------------------------------------------------------------
#  Streamer wrapper
# ---------------------------------------------------------------------------

class RaytilesStreamer:
    """Owns a `RaytilesStreamer*`. Requires a live raylib GL context
    (`InitWindow` first) at construction time.

    All configs are optional; pass `None` to use the C++ default.
    """

    def __init__(
        self,
        world: Optional[RaytilesWorldConfig] = None,
        streaming: Optional[RaytilesStreamingConfig] = None,
        rendering: Optional[RaytilesRenderingConfig] = None,
        pool: Optional[RaytilesPoolConfig] = None,
    ):
        def _ptr(cfg, cls):
            if cfg is None:
                return ctypes.POINTER(cls)()
            return ctypes.byref(cfg)

        handle = _RaytilesStreamerCreate(
            _ptr(world, RaytilesWorldConfig),
            _ptr(streaming, RaytilesStreamingConfig),
            _ptr(rendering, RaytilesRenderingConfig),
            _ptr(pool, RaytilesPoolConfig),
        )
        if not handle:
            raise RuntimeError("RaytilesStreamerCreate returned NULL")
        self._handle = handle

    def __del__(self):
        self.destroy()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.destroy()
        return False

    def destroy(self) -> None:
        handle = getattr(self, "_handle", None)
        if handle:
            _RaytilesStreamerDestroy(handle)
            self._handle = _StreamerPtr()

    def _check(self):
        if not self._handle:
            raise RuntimeError("RaytilesStreamer has been destroyed")

    # Frame loop -----------------------------------------------------------
    def update(self, camera: Camera3D) -> None:
        self._check()
        _RaytilesStreamerUpdate(self._handle, camera)

    def draw(self, camera: Camera3D) -> None:
        self._check()
        _RaytilesStreamerDraw(self._handle, camera)

    def debug(self, camera: Camera3D) -> None:
        self._check()
        _RaytilesStreamerDebug(self._handle, camera)

    def debug_3d(self) -> None:
        self._check()
        _RaytilesStreamerDebug3D(self._handle)

    # Queries --------------------------------------------------------------
    def get_renderer(self) -> RaytilesRenderer:
        self._check()
        return RaytilesRenderer(_RaytilesStreamerGetRenderer(self._handle))

    def is_loading(self) -> bool:
        self._check()
        return bool(_RaytilesStreamerIsLoading(self._handle))

    def get_loading(self) -> float:
        self._check()
        return float(_RaytilesStreamerGetLoading(self._handle))

    def ground_height(self, position: Vector3) -> Optional[float]:
        """Returns the terrain altitude under `position`, or None if no
        loaded tile covers that XZ point."""
        self._check()
        out = ctypes.c_float(0.0)
        ok = _RaytilesStreamerGroundHeight(self._handle, position, ctypes.byref(out))
        return float(out.value) if ok else None


__all__ = [
    # raylib types
    "Vector2", "Vector3", "Vector4", "Color", "Camera3D",
    # config structs
    "RaytilesWorldConfig", "RaytilesStreamingConfig",
    "RaytilesRenderingConfig", "RaytilesPoolConfig",
    # defaults
    "world_config_default", "streaming_config_default",
    "rendering_config_default", "pool_config_default",
    # main classes
    "RaytilesStreamer", "RaytilesRenderer",
    # dylib handle (also hosts raylib's re-exported symbols)
    "lib",
]