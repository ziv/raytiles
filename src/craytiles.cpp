/// @file craytiles.cpp
/// Implementation of the C wrapper declared in craytiles.h. Translates C
/// structs / opaque handles into raytiles::streamer calls.
#include "../include/raytiles/craytiles.h"

#include <cstddef>
#include <new>
#include <string>
#include <utility>

#include "../include/raytiles/raytiles.h"

static_assert(RAYTILES_ZOOM_LEVELS == raytiles::zoom_levels,
              "RAYTILES_ZOOM_LEVELS must match raytiles::zoom_levels");

// ---------------------------------------------------------------------------
//  Opaque handle types
// ---------------------------------------------------------------------------

struct RaytilesStreamer {
    raytiles::streamer impl;

    explicit RaytilesStreamer(raytiles::config conf)
        : impl(std::move(conf)) {
    }

    RaytilesStreamer(const double latitude, const double longitude, raytiles::config conf)
        : impl(latitude, longitude, std::move(conf)) {
    }
};

// ---------------------------------------------------------------------------
//  C -> C++ struct conversion helpers
// ---------------------------------------------------------------------------

namespace {
    raytiles::rendering_config to_cpp_rendering(const RaytilesRenderingConfig &c) {
        raytiles::rendering_config r{};
        r.fog_start = c.fog_start;
        r.fog_end = c.fog_end;
        r.skirt_drop = c.skirt_drop;
        r.fog_color = c.fog_color;
        r.ambient_light = c.ambient_light;
        r.sun_direction = c.sun_direction;
        r.sun_scale = c.sun_scale;
        r.height_scale = c.height_scale;
        r.normals_scale = c.normals_scale;
        return r;
    }

    raytiles::config to_cpp_config(const RaytilesConfig *c) {
        raytiles::config conf{};
        if (!c) return conf;

        conf.world.anchor_x_tile = c->world.anchor_x_tile;
        conf.world.anchor_z_tile = c->world.anchor_z_tile;
        conf.world.base_zoom = c->world.base_zoom;
        conf.world.max_zoom = c->world.max_zoom;
        conf.world.tile_size = c->world.tile_size;
        for (std::size_t i = 0; i < raytiles::zoom_levels; ++i) conf.world.skirt_overlap[i] = c->world.skirt_overlap[i];
        conf.world.mipmaps = c->world.mipmaps;
        conf.world.origin_offset = c->world.origin_offset;

        conf.streaming.radius = c->streaming.radius;
        for (std::size_t i = 0; i < raytiles::zoom_levels; ++i) conf.streaming.thresholds[i] = c->streaming.thresholds[i];
        conf.streaming.update_distance = c->streaming.update_distance;
        conf.streaming.upload_budget_sec = c->streaming.upload_budget_sec;
        conf.streaming.max_uploads_per_frame = c->streaming.max_uploads_per_frame;
        conf.streaming.near_plane = c->streaming.near_plane;
        conf.streaming.far_plane = c->streaming.far_plane;

        conf.rendering = to_cpp_rendering(c->rendering);

        conf.network.threads = c->network.threads;
        conf.network.allow_insecure_tls = c->network.allow_insecure_tls;
        conf.network.connection_timeout_sec = c->network.connection_timeout_sec;
        conf.network.read_timeout_sec = c->network.read_timeout_sec;
        if (c->network.cache_dir) conf.network.cache_dir = c->network.cache_dir;
        if (c->network.texture_url) conf.network.texture_url = c->network.texture_url;
        if (c->network.heightmap_url) conf.network.heightmap_url = c->network.heightmap_url;
        if (c->network.normals_url) conf.network.normals_url = c->network.normals_url;
        return conf;
    }
}

extern "C" {

// ---------------------------------------------------------------------------
//  Defaults
// ---------------------------------------------------------------------------

RaytilesConfig RaytilesConfigDefault(void) {
    // The string fields below are c_str() pointers into a static config;
    // valid for the lifetime of the process.
    static const raytiles::config d{};

    RaytilesConfig out{};
    out.world.anchor_x_tile = d.world.anchor_x_tile;
    out.world.anchor_z_tile = d.world.anchor_z_tile;
    out.world.base_zoom = d.world.base_zoom;
    out.world.max_zoom = d.world.max_zoom;
    out.world.tile_size = d.world.tile_size;
    for (std::size_t i = 0; i < raytiles::zoom_levels; ++i) out.world.skirt_overlap[i] = d.world.skirt_overlap[i];
    out.world.mipmaps = d.world.mipmaps;
    out.world.origin_offset = d.world.origin_offset;

    out.streaming.radius = d.streaming.radius;
    for (std::size_t i = 0; i < raytiles::zoom_levels; ++i) out.streaming.thresholds[i] = d.streaming.thresholds[i];
    out.streaming.update_distance = d.streaming.update_distance;
    out.streaming.upload_budget_sec = d.streaming.upload_budget_sec;
    out.streaming.max_uploads_per_frame = d.streaming.max_uploads_per_frame;
    out.streaming.near_plane = d.streaming.near_plane;
    out.streaming.far_plane = d.streaming.far_plane;

    out.rendering.fog_start = d.rendering.fog_start;
    out.rendering.fog_end = d.rendering.fog_end;
    out.rendering.skirt_drop = d.rendering.skirt_drop;
    out.rendering.fog_color = d.rendering.fog_color;
    out.rendering.ambient_light = d.rendering.ambient_light;
    out.rendering.sun_direction = d.rendering.sun_direction;
    out.rendering.sun_scale = d.rendering.sun_scale;
    out.rendering.height_scale = d.rendering.height_scale;
    out.rendering.normals_scale = d.rendering.normals_scale;

    out.network.threads = d.network.threads;
    out.network.allow_insecure_tls = d.network.allow_insecure_tls;
    out.network.connection_timeout_sec = d.network.connection_timeout_sec;
    out.network.read_timeout_sec = d.network.read_timeout_sec;
    out.network.cache_dir = d.network.cache_dir.c_str();
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
        return new RaytilesStreamer(to_cpp_config(config));
    } catch (...) {
        return nullptr;
    }
}

RaytilesStreamer *RaytilesStreamerCreateLatLon(const double latitude,
                                               const double longitude,
                                               const RaytilesConfig *config) {
    try {
        return new RaytilesStreamer(latitude, longitude, to_cpp_config(config));
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

Vector3 RaytilesStreamerInitialPosition(const RaytilesStreamer *streamer, const float altitude) {
    if (!streamer) return Vector3{0.0f, 0.0f, 0.0f};
    return streamer->impl.initial_position(altitude);
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
//  Rendering parameter setters
// ---------------------------------------------------------------------------

void RaytilesStreamerSetRendering(RaytilesStreamer *streamer, const RaytilesRenderingConfig rendering) {
    if (!streamer) return;
    streamer->impl.set_rendering(to_cpp_rendering(rendering));
}

void RaytilesStreamerSetFogColor(RaytilesStreamer *streamer, const Color color) {
    if (!streamer) return;
    streamer->impl.set_fog_color(color);
}

void RaytilesStreamerSetFogStart(RaytilesStreamer *streamer, const float distance) {
    if (!streamer) return;
    streamer->impl.set_fog_start(distance);
}

void RaytilesStreamerSetFogEnd(RaytilesStreamer *streamer, const float distance) {
    if (!streamer) return;
    streamer->impl.set_fog_end(distance);
}

void RaytilesStreamerSetAmbientLight(RaytilesStreamer *streamer, const Color color) {
    if (!streamer) return;
    streamer->impl.set_ambient_light(color);
}

void RaytilesStreamerSetSunDirection(RaytilesStreamer *streamer, const Vector3 direction) {
    if (!streamer) return;
    streamer->impl.set_sun_direction(direction);
}

void RaytilesStreamerSetSunScale(RaytilesStreamer *streamer, const float scale) {
    if (!streamer) return;
    streamer->impl.set_sun_scale(scale);
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
