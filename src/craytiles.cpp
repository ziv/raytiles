/// @file craytiles.cpp
/// Implementation of the C wrapper declared in craytiles.h. Translates C
/// structs / opaque handles into raytiles::streamer + raytiles::renderer calls.
#include "../include/raytiles/craytiles.h"

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

    RaytilesStreamer(raytiles::world_config w,
                     raytiles::streaming_config s,
                     raytiles::rendering_config r,
                     raytiles::pool_config p)
        : impl(std::move(w), std::move(s), std::move(r), std::move(p)) {
    }
};

// ---------------------------------------------------------------------------
//  C -> C++ struct conversion helpers
// ---------------------------------------------------------------------------

namespace {
    raytiles::world_config to_cpp_world(const RaytilesWorldConfig *c) {
        raytiles::world_config w{};
        if (!c) return w;
        w.anchor_x_tile = c->anchor_x_tile;
        w.anchor_z_tile = c->anchor_z_tile;
        w.base_zoom = c->base_zoom;
        w.max_zoom = c->max_zoom;
        w.base_zoom_tile_size = c->base_zoom_tile_size;
        w.use_mipmap = c->use_mipmap;
        for (std::size_t i = 0; i < raytiles::zoom_levels; ++i) {
            w.skirt_overlap[i] = c->skirt_overlap[i];
        }
        return w;
    }

    raytiles::streaming_config to_cpp_streaming(const RaytilesStreamingConfig *c) {
        raytiles::streaming_config s{};
        if (!c) return s;
        s.rendering_radius = c->rendering_radius;
        s.update_distance_sq = c->update_distance_sq;
        s.upload_budget_sec = c->upload_budget_sec;
        s.max_uploads_per_frame = c->max_uploads_per_frame;
        s.near_plane = c->near_plane;
        s.far_plane = c->far_plane;
        for (std::size_t i = 0; i < raytiles::zoom_levels; ++i) {
            s.thresholds[i] = c->thresholds[i];
        }
        return s;
    }

    raytiles::rendering_config to_cpp_rendering(const RaytilesRenderingConfig *c) {
        raytiles::rendering_config r{};
        if (!c) return r;
        r.fog_start = c->fog_start;
        r.fog_end = c->fog_end;
        r.skirt_drop = c->skirt_drop;
        for (int i = 0; i < 4; ++i) r.fog_color[i] = c->fog_color[i];
        for (int i = 0; i < 4; ++i) r.ambient_light[i] = c->ambient_light[i];
        for (int i = 0; i < 3; ++i) r.sun_direction[i] = c->sun_direction[i];
        r.sun_scale = c->sun_scale;
        r.height_scale = c->height_scale;
        r.normals_scale = c->normals_scale;
        return r;
    }

    raytiles::pool_config to_cpp_pool(const RaytilesPoolConfig *c) {
        raytiles::pool_config p{};
        if (!c) return p;
        p.download_threads = c->download_threads;
        p.allow_insecure_tls = c->allow_insecure_tls;
        if (c->texture_cache_path) p.texture_cache_path = c->texture_cache_path;
        if (c->heightmap_cache_path) p.heightmap_cache_path = c->heightmap_cache_path;
        if (c->normals_cache_path) p.normals_cache_path = c->normals_cache_path;
        if (c->texture_url) p.texture_url = c->texture_url;
        if (c->heightmap_url) p.heightmap_url = c->heightmap_url;
        if (c->normals_url) p.normals_url = c->normals_url;
        return p;
    }
}

extern "C" {

// ---------------------------------------------------------------------------
//  Default-initializers
// ---------------------------------------------------------------------------

RaytilesWorldConfig RaytilesWorldConfigDefault(void) {
    const raytiles::world_config w{};
    RaytilesWorldConfig out{};
    out.anchor_x_tile = w.anchor_x_tile;
    out.anchor_z_tile = w.anchor_z_tile;
    out.base_zoom = w.base_zoom;
    out.max_zoom = w.max_zoom;
    out.base_zoom_tile_size = w.base_zoom_tile_size;
    for (std::size_t i = 0; i < raytiles::zoom_levels; ++i) {
        out.skirt_overlap[i] = w.skirt_overlap[i];
    }
    out.use_mipmap = w.use_mipmap;
    return out;
}

RaytilesStreamingConfig RaytilesStreamingConfigDefault(void) {
    const raytiles::streaming_config s{};
    RaytilesStreamingConfig out{};
    out.rendering_radius = s.rendering_radius;
    for (std::size_t i = 0; i < raytiles::zoom_levels; ++i) {
        out.thresholds[i] = s.thresholds[i];
    }
    out.update_distance_sq = s.update_distance_sq;
    out.upload_budget_sec = s.upload_budget_sec;
    out.max_uploads_per_frame = s.max_uploads_per_frame;
    out.near_plane = s.near_plane;
    out.far_plane = s.far_plane;
    return out;
}

RaytilesRenderingConfig RaytilesRenderingConfigDefault(void) {
    constexpr raytiles::rendering_config r{};
    RaytilesRenderingConfig out{};
    out.fog_start = r.fog_start;
    out.fog_end = r.fog_end;
    out.skirt_drop = r.skirt_drop;
    for (int i = 0; i < 4; ++i) out.fog_color[i] = r.fog_color[i];
    for (int i = 0; i < 4; ++i) out.ambient_light[i] = r.ambient_light[i];
    for (int i = 0; i < 3; ++i) out.sun_direction[i] = r.sun_direction[i];
    out.sun_scale = r.sun_scale;
    out.height_scale = r.height_scale;
    out.normals_scale = r.normals_scale;
    return out;
}

RaytilesPoolConfig RaytilesPoolConfigDefault(void) {
    // The string fields below are c_str() pointers into the static
    // pool_config; valid for the lifetime of the process.
    static const raytiles::pool_config d{};
    RaytilesPoolConfig out{};
    out.download_threads = d.download_threads;
    out.allow_insecure_tls = d.allow_insecure_tls;
    out.texture_cache_path = d.texture_cache_path.c_str();
    out.heightmap_cache_path = d.heightmap_cache_path.c_str();
    out.normals_cache_path = d.normals_cache_path.c_str();
    out.texture_url = d.texture_url.c_str();
    out.heightmap_url = d.heightmap_url.c_str();
    out.normals_url = d.normals_url.c_str();
    return out;
}

// ---------------------------------------------------------------------------
//  Streamer
// ---------------------------------------------------------------------------

RaytilesStreamer *RaytilesStreamerCreate(const RaytilesWorldConfig *world,
                                         const RaytilesStreamingConfig *streaming,
                                         const RaytilesRenderingConfig *rendering,
                                         const RaytilesPoolConfig *pool) {
    try {
        return new RaytilesStreamer(to_cpp_world(world),
                                    to_cpp_streaming(streaming),
                                    to_cpp_rendering(rendering),
                                    to_cpp_pool(pool));
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

bool RaytilesStreamerIsLoading(const RaytilesStreamer *streamer) {
    if (!streamer) return false;
    return streamer->impl.is_loading();
}

float RaytilesStreamerGetLoading(const RaytilesStreamer *streamer) {
    if (!streamer) return 0.0f;
    return streamer->impl.get_loading();
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

void RaytilesStreamerSetAmbientLightV4(RaytilesStreamer *streamer, const Vector4 color) {
    if (!streamer) return;
    streamer->impl.set_ambient_light(color);
}

void RaytilesStreamerSetAmbientLightRGBA(RaytilesStreamer *streamer,
                                         const float r, const float g, const float b, const float a) {
    if (!streamer) return;
    streamer->impl.set_ambient_light(r, g, b, a);
}

void RaytilesStreamerSetFogColor(RaytilesStreamer *streamer, const Color color) {
    if (!streamer) return;
    streamer->impl.set_fog_color(color);
}

void RaytilesStreamerSetFogColorV4(RaytilesStreamer *streamer, const Vector4 color) {
    if (!streamer) return;
    streamer->impl.set_fog_color(color);
}

void RaytilesStreamerSetFogColorRGBA(RaytilesStreamer *streamer,
                                     const float r, const float g, const float b, const float a) {
    if (!streamer) return;
    streamer->impl.set_fog_color(r, g, b, a);
}

void RaytilesStreamerSetFogStart(RaytilesStreamer *streamer, const float distance) {
    if (!streamer) return;
    streamer->impl.set_fog_start(distance);
}

void RaytilesStreamerSetFogEnd(RaytilesStreamer *streamer, const float distance) {
    if (!streamer) return;
    streamer->impl.set_fog_end(distance);
}

void RaytilesStreamerSetHeightScale(RaytilesStreamer *streamer, const float scale) {
    if (!streamer) return;
    streamer->impl.set_height_scale(scale);
}

void RaytilesStreamerSetNormalsScale(RaytilesStreamer *streamer, const float scale) {
    if (!streamer) return;
    streamer->impl.set_normals_scale(scale);
}

void RaytilesStreamerSetSunDirection(RaytilesStreamer *streamer, const Vector3 direction) {
    if (!streamer) return;
    streamer->impl.set_sun_direction(direction);
}

void RaytilesStreamerSetSunScale(RaytilesStreamer *streamer, const float scale) {
    if (!streamer) return;
    streamer->impl.set_sun_scale(scale);
}

} // extern "C"