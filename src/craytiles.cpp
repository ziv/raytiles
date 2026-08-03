/// @file craytiles.cpp
/// Implementation of the C wrapper declared in craytiles.h. Translates the C
/// config struct / opaque handle into raytiles::streamer calls.
#include "../include/raytiles/craytiles.h"

#include <utility>

#include "../include/raytiles/raytiles.h"

static_assert(RAYTILES_ZOOM_LEVELS == raytiles::zoom_levels,
              "RAYTILES_ZOOM_LEVELS must match raytiles::zoom_levels");

// ---------------------------------------------------------------------------
//  Opaque handle type
// ---------------------------------------------------------------------------

struct RaytilesStreamer {
    raytiles::streamer impl;

    explicit RaytilesStreamer(raytiles::config cfg)
        : impl(std::move(cfg)) {
    }

    RaytilesStreamer(const double latitude, const double longitude, raytiles::config cfg)
        : impl(latitude, longitude, std::move(cfg)) {
    }
};

// ---------------------------------------------------------------------------
//  C -> C++ struct conversion
// ---------------------------------------------------------------------------

namespace {
    raytiles::config to_cpp(const RaytilesConfig *c) {
        raytiles::config cfg{};
        if (!c) return cfg;

        cfg.world.anchor_x_tile = c->world.anchor_x_tile;
        cfg.world.anchor_z_tile = c->world.anchor_z_tile;
        cfg.world.base_zoom = c->world.base_zoom;
        cfg.world.max_zoom = c->world.max_zoom;
        cfg.world.base_zoom_tile_size = c->world.base_zoom_tile_size;
        cfg.world.use_mipmap = c->world.use_mipmap;
        cfg.world.offset = c->world.offset;
        for (std::size_t i = 0; i < raytiles::zoom_levels; ++i) {
            cfg.world.skirt_overlap[i] = c->world.skirt_overlap[i];
        }

        cfg.streaming.rendering_radius = c->streaming.rendering_radius;
        cfg.streaming.update_distance = c->streaming.update_distance;
        cfg.streaming.upload_budget_sec = c->streaming.upload_budget_sec;
        cfg.streaming.max_uploads_per_frame = c->streaming.max_uploads_per_frame;
        cfg.streaming.near_plane = c->streaming.near_plane;
        cfg.streaming.far_plane = c->streaming.far_plane;
        for (std::size_t i = 0; i < raytiles::zoom_levels; ++i) {
            cfg.streaming.thresholds[i] = c->streaming.thresholds[i];
        }

        cfg.rendering.fog_start = c->rendering.fog_start;
        cfg.rendering.fog_end = c->rendering.fog_end;
        cfg.rendering.skirt_drop = c->rendering.skirt_drop;
        for (int i = 0; i < 4; ++i) cfg.rendering.fog_color[i] = c->rendering.fog_color[i];
        for (int i = 0; i < 4; ++i) cfg.rendering.ambient_light[i] = c->rendering.ambient_light[i];
        for (int i = 0; i < 3; ++i) cfg.rendering.sun_direction[i] = c->rendering.sun_direction[i];
        cfg.rendering.sun_scale = c->rendering.sun_scale;
        cfg.rendering.height_scale = c->rendering.height_scale;
        cfg.rendering.normals_scale = c->rendering.normals_scale;

        cfg.network.download_threads = c->network.download_threads;
        cfg.network.allow_insecure_tls = c->network.allow_insecure_tls;
        // NULL string = keep the built-in default already present in cfg
        if (c->network.texture_cache_path) cfg.network.texture_cache_path = c->network.texture_cache_path;
        if (c->network.heightmap_cache_path) cfg.network.heightmap_cache_path = c->network.heightmap_cache_path;
        if (c->network.normals_cache_path) cfg.network.normals_cache_path = c->network.normals_cache_path;
        if (c->network.texture_url) cfg.network.texture_url = c->network.texture_url;
        if (c->network.heightmap_url) cfg.network.heightmap_url = c->network.heightmap_url;
        if (c->network.normals_url) cfg.network.normals_url = c->network.normals_url;

        return cfg;
    }
}

extern "C" {

// ---------------------------------------------------------------------------
//  Default-initializer
// ---------------------------------------------------------------------------

RaytilesConfig RaytilesConfigDefault(void) {
    // The string fields below are c_str() pointers into this static config;
    // valid for the lifetime of the process.
    static const raytiles::config d{};
    RaytilesConfig out{};

    out.world.anchor_x_tile = d.world.anchor_x_tile;
    out.world.anchor_z_tile = d.world.anchor_z_tile;
    out.world.base_zoom = d.world.base_zoom;
    out.world.max_zoom = d.world.max_zoom;
    out.world.base_zoom_tile_size = d.world.base_zoom_tile_size;
    for (std::size_t i = 0; i < raytiles::zoom_levels; ++i) {
        out.world.skirt_overlap[i] = d.world.skirt_overlap[i];
    }
    out.world.use_mipmap = d.world.use_mipmap;
    out.world.offset = d.world.offset;

    out.streaming.rendering_radius = d.streaming.rendering_radius;
    for (std::size_t i = 0; i < raytiles::zoom_levels; ++i) {
        out.streaming.thresholds[i] = d.streaming.thresholds[i];
    }
    out.streaming.update_distance = d.streaming.update_distance;
    out.streaming.upload_budget_sec = d.streaming.upload_budget_sec;
    out.streaming.max_uploads_per_frame = d.streaming.max_uploads_per_frame;
    out.streaming.near_plane = d.streaming.near_plane;
    out.streaming.far_plane = d.streaming.far_plane;

    out.rendering.fog_start = d.rendering.fog_start;
    out.rendering.fog_end = d.rendering.fog_end;
    out.rendering.skirt_drop = d.rendering.skirt_drop;
    for (int i = 0; i < 4; ++i) out.rendering.fog_color[i] = d.rendering.fog_color[i];
    for (int i = 0; i < 4; ++i) out.rendering.ambient_light[i] = d.rendering.ambient_light[i];
    for (int i = 0; i < 3; ++i) out.rendering.sun_direction[i] = d.rendering.sun_direction[i];
    out.rendering.sun_scale = d.rendering.sun_scale;
    out.rendering.height_scale = d.rendering.height_scale;
    out.rendering.normals_scale = d.rendering.normals_scale;

    out.network.download_threads = d.network.download_threads;
    out.network.allow_insecure_tls = d.network.allow_insecure_tls;
    out.network.texture_cache_path = d.network.texture_cache_path.c_str();
    out.network.heightmap_cache_path = d.network.heightmap_cache_path.c_str();
    out.network.normals_cache_path = d.network.normals_cache_path.c_str();
    out.network.texture_url = d.network.texture_url.c_str();
    out.network.heightmap_url = d.network.heightmap_url.c_str();
    out.network.normals_url = d.network.normals_url.c_str();

    return out;
}

// ---------------------------------------------------------------------------
//  Streamer
// ---------------------------------------------------------------------------

RaytilesStreamer *RaytilesStreamerCreate(const RaytilesConfig *config) {
    try {
        return new RaytilesStreamer(to_cpp(config));
    } catch (...) {
        return nullptr;
    }
}

RaytilesStreamer *RaytilesStreamerCreateLatLon(const double latitude,
                                               const double longitude,
                                               const RaytilesConfig *config) {
    try {
        return new RaytilesStreamer(latitude, longitude, to_cpp(config));
    } catch (...) {
        return nullptr;
    }
}

void RaytilesStreamerDestroy(RaytilesStreamer *streamer) {
    delete streamer;
}

void RaytilesStreamerUpdate(RaytilesStreamer *streamer, const Camera3D camera, const Vector3 worldOffset) {
    if (!streamer) return;
    streamer->impl.update(camera, worldOffset);
}

void RaytilesStreamerDraw(RaytilesStreamer *streamer) {
    if (!streamer) return;
    streamer->impl.draw();
}

void RaytilesStreamerDrawDebug3D(RaytilesStreamer *streamer) {
    if (!streamer) return;
    streamer->impl.draw_debug_3d();
}

void RaytilesStreamerDrawDebugLabels(RaytilesStreamer *streamer) {
    if (!streamer) return;
    streamer->impl.draw_debug_labels();
}

bool RaytilesStreamerIsLoading(const RaytilesStreamer *streamer) {
    if (!streamer) return false;
    return streamer->impl.is_loading();
}

float RaytilesStreamerLoadingProgress(const RaytilesStreamer *streamer) {
    if (!streamer) return 0.0f;
    return streamer->impl.loading_progress();
}

Vector3 RaytilesStreamerGetInitialPosition(const RaytilesStreamer *streamer, const float y) {
    if (!streamer) return Vector3{0.0f, 0.0f, 0.0f};
    return streamer->impl.get_initial_position(y);
}

bool RaytilesStreamerGroundHeight(const RaytilesStreamer *streamer,
                                  const Vector3 position,
                                  float *out_height) {
    if (!streamer) return false;
    const auto h = streamer->impl.ground_height(position);
    if (!h.has_value()) return false;
    if (out_height) *out_height = *h;
    return true;
}

// ---------------------------------------------------------------------------
//  Shader parameter setters
// ---------------------------------------------------------------------------

void RaytilesStreamerSetAmbientLight(RaytilesStreamer *streamer, const Color color) {
    if (!streamer) return;
    streamer->impl.set_ambient_light(color);
}

void RaytilesStreamerSetFogColor(RaytilesStreamer *streamer, const Color color) {
    if (!streamer) return;
    streamer->impl.set_fog_color(color);
}

void RaytilesStreamerSetFog(RaytilesStreamer *streamer, const Color color, const float start, const float end) {
    if (!streamer) return;
    streamer->impl.set_fog(color, start, end);
}

void RaytilesStreamerSetSun(RaytilesStreamer *streamer, const Vector3 direction, const float intensity) {
    if (!streamer) return;
    streamer->impl.set_sun(direction, intensity);
}

void RaytilesStreamerSetHeightScale(RaytilesStreamer *streamer, const float scale) {
    if (!streamer) return;
    streamer->impl.set_height_scale(scale);
}

void RaytilesStreamerSetNormalsScale(RaytilesStreamer *streamer, const float scale) {
    if (!streamer) return;
    streamer->impl.set_normals_scale(scale);
}

} // extern "C"
