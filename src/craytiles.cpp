/// @file craytiles.cpp
/// Implementation of the C wrapper declared in craytiles.h. Translates C
/// structs / opaque handles into raytiles::streamer + raytiles::renderer calls.
#include "../include/raytiles/craytiles.h"

#include <algorithm>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "../include/raytiles/raytiles.h"

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

// RaytilesRenderer is just an alias for raytiles::renderer at the ABI level;
// reinterpret_cast through these helpers keeps the public header free of any
// C++ types.
namespace {
    raytiles::renderer *to_cpp(RaytilesRenderer *r) {
        return reinterpret_cast<raytiles::renderer *>(r);
    }

    RaytilesRenderer *to_c(raytiles::renderer *r) {
        return reinterpret_cast<RaytilesRenderer *>(r);
    }

    std::string to_string_or_empty(const char *s) {
        return s ? std::string(s) : std::string();
    }
}

// ---------------------------------------------------------------------------
//  Library-owned static storage for per-zoom defaults
// ---------------------------------------------------------------------------

namespace {
    struct zoom_map_storage {
        std::vector<int> zooms;
        std::vector<float> values;
    };

    template<typename Map>
    zoom_map_storage flatten_sorted(const Map &m) {
        zoom_map_storage out;
        out.zooms.reserve(m.size());
        out.values.reserve(m.size());
        // sort by zoom so callers iterating the arrays get a stable order
        std::vector<std::pair<int, float> > entries(m.begin(), m.end());
        std::sort(entries.begin(), entries.end(),
                  [](const auto &a, const auto &b) { return a.first < b.first; });
        for (const auto &[zoom, value] : entries) {
            out.zooms.push_back(zoom);
            out.values.push_back(value);
        }
        return out;
    }

    const zoom_map_storage &default_thresholds() {
        static const zoom_map_storage storage =
                flatten_sorted(raytiles::streaming_config{}.thresholds);
        return storage;
    }

    const zoom_map_storage &default_skirt_overlap() {
        static const zoom_map_storage storage =
                flatten_sorted(raytiles::world_config{}.skirt_overlap);
        return storage;
    }
}

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
        w.use_logger = c->use_logger;
        if (c->skirt_overlap_zooms && c->skirt_overlap_values && c->skirt_overlap_count > 0) {
            w.skirt_overlap.clear();
            for (int i = 0; i < c->skirt_overlap_count; ++i) {
                w.skirt_overlap[c->skirt_overlap_zooms[i]] = c->skirt_overlap_values[i];
            }
        }
        return w;
    }

    raytiles::streaming_config to_cpp_streaming(const RaytilesStreamingConfig *c) {
        raytiles::streaming_config s{};
        if (!c) return s;
        s.rendering_radius = c->rendering_radius;
        s.update_distance_sq = c->update_distance_sq;
        s.update_height = c->update_height;
        s.upload_budget_sec = c->upload_budget_sec;
        s.max_uploads_per_frame = c->max_uploads_per_frame;
        s.near_plane = c->near_plane;
        s.far_plane = c->far_plane;
        if (c->threshold_zooms && c->threshold_values && c->thresholds_count > 0) {
            s.thresholds.clear();
            for (int i = 0; i < c->thresholds_count; ++i) {
                s.thresholds[c->threshold_zooms[i]] = c->threshold_values[i];
            }
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
        p.use_logger = c->use_logger;
        if (c->texture_cache_path) p.texture_cache_path = c->texture_cache_path;
        if (c->heightmap_cache_path) p.heightmap_cache_path = c->heightmap_cache_path;
        if (c->normals_cache_path) p.normals_cache_path = c->normals_cache_path;
        if (c->texture_url) p.texture_url = c->texture_url;
        if (c->heightmap_url) p.heightmap_url = c->heightmap_url;
        if (c->normals_url) p.normals_url = c->normals_url;
        p.texture_host = to_string_or_empty(c->texture_host);
        p.texture_url_path = to_string_or_empty(c->texture_url_path);
        p.heightmap_host = to_string_or_empty(c->heightmap_host);
        p.heightmap_url_path = to_string_or_empty(c->heightmap_url_path);
        p.normals_host = to_string_or_empty(c->normals_host);
        p.normals_url_path = to_string_or_empty(c->normals_url_path);
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
    const auto &skirt = default_skirt_overlap();
    out.skirt_overlap_zooms = skirt.zooms.data();
    out.skirt_overlap_values = skirt.values.data();
    out.skirt_overlap_count = static_cast<int>(skirt.zooms.size());
    out.use_mipmap = w.use_mipmap;
    out.use_logger = w.use_logger;
    return out;
}

RaytilesStreamingConfig RaytilesStreamingConfigDefault(void) {
    const raytiles::streaming_config s{};
    RaytilesStreamingConfig out{};
    out.rendering_radius = s.rendering_radius;
    const auto &thresholds = default_thresholds();
    out.threshold_zooms = thresholds.zooms.data();
    out.threshold_values = thresholds.values.data();
    out.thresholds_count = static_cast<int>(thresholds.zooms.size());
    out.update_distance_sq = s.update_distance_sq;
    out.update_height = s.update_height;
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
    out.use_logger = d.use_logger;
    out.texture_cache_path = d.texture_cache_path.c_str();
    out.heightmap_cache_path = d.heightmap_cache_path.c_str();
    out.normals_cache_path = d.normals_cache_path.c_str();
    out.texture_url = d.texture_url.c_str();
    out.heightmap_url = d.heightmap_url.c_str();
    out.normals_url = d.normals_url.c_str();
    out.texture_host = d.texture_host.c_str();
    out.texture_url_path = d.texture_url_path.c_str();
    out.heightmap_host = d.heightmap_host.c_str();
    out.heightmap_url_path = d.heightmap_url_path.c_str();
    out.normals_host = d.normals_host.c_str();
    out.normals_url_path = d.normals_url_path.c_str();
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

void RaytilesStreamerUpdate(RaytilesStreamer *streamer, const Camera3D camera) {
    if (!streamer) return;
    streamer->impl.update(camera);
}

void RaytilesStreamerDraw(RaytilesStreamer *streamer, const Camera3D camera) {
    if (!streamer) return;
    streamer->impl.draw(camera);
}

void RaytilesStreamerDebug(RaytilesStreamer *streamer, const Camera3D camera) {
    if (!streamer) return;
    streamer->impl.debug(camera);
}

void RaytilesStreamerDebug3D(RaytilesStreamer *streamer) {
    if (!streamer) return;
    streamer->impl.debug_3d();
}

RaytilesRenderer *RaytilesStreamerGetRenderer(RaytilesStreamer *streamer) {
    if (!streamer) return nullptr;
    return to_c(&streamer->impl.get_renderer());
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
//  Renderer
// ---------------------------------------------------------------------------

void RaytilesRendererSetAmbientLight(RaytilesRenderer *renderer, const Color color) {
    if (!renderer) return;
    to_cpp(renderer)->set_ambient_light(color);
}

void RaytilesRendererSetAmbientLightV4(RaytilesRenderer *renderer, const Vector4 color) {
    if (!renderer) return;
    to_cpp(renderer)->set_ambient_light(color);
}

void RaytilesRendererSetAmbientLightRGBA(RaytilesRenderer *renderer,
                                         const float r, const float g, const float b, const float a) {
    if (!renderer) return;
    to_cpp(renderer)->set_ambient_light(r, g, b, a);
}

void RaytilesRendererSetFogColor(RaytilesRenderer *renderer, const Color color) {
    if (!renderer) return;
    to_cpp(renderer)->set_fog_color(color);
}

void RaytilesRendererSetFogColorV4(RaytilesRenderer *renderer, const Vector4 color) {
    if (!renderer) return;
    to_cpp(renderer)->set_fog_color(color);
}

void RaytilesRendererSetFogColorRGBA(RaytilesRenderer *renderer,
                                     const float r, const float g, const float b, const float a) {
    if (!renderer) return;
    to_cpp(renderer)->set_fog_color(r, g, b, a);
}

void RaytilesRendererSetFogStart(RaytilesRenderer *renderer, const float distance) {
    if (!renderer) return;
    to_cpp(renderer)->set_fog_start(distance);
}

void RaytilesRendererSetFogEnd(RaytilesRenderer *renderer, const float distance) {
    if (!renderer) return;
    to_cpp(renderer)->set_fog_end(distance);
}

void RaytilesRendererSetHeightScale(RaytilesRenderer *renderer, const float scale) {
    if (!renderer) return;
    to_cpp(renderer)->set_height_scale(scale);
}

void RaytilesRendererSetNormalsScale(RaytilesRenderer *renderer, const float scale) {
    if (!renderer) return;
    to_cpp(renderer)->set_normals_scale(scale);
}

void RaytilesRendererSetSunDirection(RaytilesRenderer *renderer, const Vector3 direction) {
    if (!renderer) return;
    to_cpp(renderer)->set_sun_direction(direction);
}

void RaytilesRendererSetSunScale(RaytilesRenderer *renderer, const float scale) {
    if (!renderer) return;
    to_cpp(renderer)->set_sun_scale(scale);
}

} // extern "C"
