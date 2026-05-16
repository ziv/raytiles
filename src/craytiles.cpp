/// @file craytiles.cpp
/// Implementation of the C wrapper declared in craytiles.h.
/// Translates C structs / opaque handles into raytiles::streamer calls.
#include "../include/raytiles/craytiles.h"

#include <algorithm>
#include <new>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

#include "../include/raytiles/raytiles.h"

namespace {
    std::string to_string_or_empty(const char *s) {
        return s ? std::string(s) : std::string();
    }
}

struct [[maybe_unused]] RaytilesStreamer {
    raytiles::streamer impl;

    RaytilesStreamer(const raytiles::world_config &w,
                     const raytiles::streaming_config &s,
                     const raytiles::rendering_config &r,
                     const raytiles::pool_config &p)
        : impl(w, s, r, p) {
    }
};

namespace {
    // Library-owned static storage for the default per-zoom thresholds.
    // Populated lazily on the first call to RaytilesConfigDefault() from the
    // C++ streaming_config defaults, so the two stay in sync automatically.
    // RaytilesConfigDefault() points its threshold_zooms / threshold_values
    // pointers at these arrays.
    struct default_thresholds_storage {
        std::vector<int> zooms;
        std::vector<float> values;
    };

    const default_thresholds_storage &default_thresholds() {
        static const default_thresholds_storage storage = [] {
            const raytiles::streaming_config s{};
            default_thresholds_storage out;
            out.zooms.reserve(s.thresholds.size());
            out.values.reserve(s.thresholds.size());
            // sort by zoom so callers iterating the arrays get a stable order
            std::vector<std::pair<int, float> > entries(s.thresholds.begin(), s.thresholds.end());
            std::sort(entries.begin(), entries.end(),
                      [](const auto &a, const auto &b) { return a.first < b.first; });
            for (const auto &[zoom, value] : entries) {
                out.zooms.push_back(zoom);
                out.values.push_back(value);
            }
            return out;
        }();
        return storage;
    }

    // Same idea as default_thresholds(), but seeded from world_config::skirt_overlap.
    const default_thresholds_storage &default_skirt_overlap() {
        static const default_thresholds_storage storage = [] {
            const raytiles::world_config w{};
            default_thresholds_storage out;
            out.zooms.reserve(w.skirt_overlap.size());
            out.values.reserve(w.skirt_overlap.size());
            std::vector<std::pair<int, float> > entries(w.skirt_overlap.begin(), w.skirt_overlap.end());
            std::sort(entries.begin(), entries.end(),
                      [](const auto &a, const auto &b) { return a.first < b.first; });
            for (const auto &[zoom, value] : entries) {
                out.zooms.push_back(zoom);
                out.values.push_back(value);
            }
            return out;
        }();
        return storage;
    }
}

extern "C" {
RaytilesConfig RaytilesConfigDefault(void) {
    const raytiles::world_config w{};
    const raytiles::streaming_config s{};
    constexpr raytiles::rendering_config r{};
    RaytilesConfig out{};
    const auto &thresholds = default_thresholds();
    out.threshold_zooms = thresholds.zooms.data();
    out.threshold_values = thresholds.values.data();
    out.thresholds_count = static_cast<int>(thresholds.zooms.size());
    const auto &skirt = default_skirt_overlap();
    out.skirt_overlap_zooms = skirt.zooms.data();
    out.skirt_overlap_values = skirt.values.data();
    out.skirt_overlap_count = static_cast<int>(skirt.zooms.size());
    out.base_zoom = w.base_zoom;
    out.max_zoom = w.max_zoom;
    out.base_zoom_tile_size = w.base_zoom_tile_size;
    out.anchor_x_tile = w.anchor_x_tile;
    out.anchor_z_tile = w.anchor_z_tile;
    out.use_mipmap = w.use_mipmap;
    out.use_logger = w.use_logger;
    out.rendering_radius = s.rendering_radius;
    out.update_distance_sq = s.update_distance_sq;
    out.update_height = s.update_height;
    out.upload_budget_sec = s.upload_budget_sec;
    out.max_uploads_per_frame = s.max_uploads_per_frame;
    out.near_plane = r.near_plane;
    out.far_plane = r.far_plane;
    out.fog_start = r.fog_start;
    out.fog_end = r.fog_end;
    out.skirt_drop = r.skirt_drop;
    out.fog_color[0] = r.fog_color[0];
    out.fog_color[1] = r.fog_color[1];
    out.fog_color[2] = r.fog_color[2];
    out.fog_color[3] = r.fog_color[3];
    out.ambient_light[0] = r.ambient_light[0];
    out.ambient_light[1] = r.ambient_light[1];
    out.ambient_light[2] = r.ambient_light[2];
    out.ambient_light[3] = r.ambient_light[3];
    out.sun_direction[0] = r.sun_direction[0];
    out.sun_direction[1] = r.sun_direction[1];
    out.sun_direction[2] = r.sun_direction[2];
    out.sun_scale = r.sun_scale;
    out.height_scale = r.height_scale;
    out.normals_scale = r.normals_scale;
    return out;
}

RaytilesPoolConfig RaytilesPoolConfigDefault(void) {
    static const raytiles::pool_config d{};
    RaytilesPoolConfig out{};
    out.download_threads = d.download_threads;
    out.allow_insecure_tls = d.allow_insecure_tls;
    out.use_logger = d.use_logger;
    out.texture_cache_path = d.texture_cache_path.c_str();
    out.heightmap_cache_path = d.heightmap_cache_path.c_str();
    out.normals_cache_path = d.normals_cache_path.c_str();
    out.texture_host = d.texture_host.c_str();
    out.texture_url_path = d.texture_url_path.c_str();
    out.heightmap_host = d.heightmap_host.c_str();
    out.heightmap_url_path = d.heightmap_url_path.c_str();
    out.normals_host = d.normals_host.c_str();
    out.normals_url_path = d.normals_url_path.c_str();
    return out;
}

RaytilesStreamer *RaytilesStreamerCreate(const RaytilesConfig *conf,
                                         const RaytilesPoolConfig *pool_conf) {
    if (!conf || !pool_conf) return nullptr;

    raytiles::world_config w{};
    w.base_zoom = conf->base_zoom;
    w.max_zoom = conf->max_zoom;
    w.base_zoom_tile_size = conf->base_zoom_tile_size;
    w.anchor_x_tile = conf->anchor_x_tile;
    w.anchor_z_tile = conf->anchor_z_tile;
    w.use_mipmap = conf->use_mipmap;
    w.use_logger = conf->use_logger;
    if (conf->skirt_overlap_zooms && conf->skirt_overlap_values) {
        w.skirt_overlap.clear();
        for (int i = 0; i < conf->skirt_overlap_count; ++i) {
            w.skirt_overlap[conf->skirt_overlap_zooms[i]] = conf->skirt_overlap_values[i];
        }
    }

    raytiles::streaming_config s{};
    s.rendering_radius = conf->rendering_radius;
    s.update_distance_sq = conf->update_distance_sq;
    s.update_height = conf->update_height;
    s.upload_budget_sec = conf->upload_budget_sec;
    s.max_uploads_per_frame = conf->max_uploads_per_frame;
    s.thresholds.clear();
    if (conf->threshold_zooms && conf->threshold_values) {
        for (int i = 0; i < conf->thresholds_count; ++i) {
            s.thresholds[conf->threshold_zooms[i]] = conf->threshold_values[i];
        }
    }

    raytiles::rendering_config r{};
    r.near_plane = conf->near_plane;
    r.far_plane = conf->far_plane;
    r.fog_start = conf->fog_start;
    r.fog_end = conf->fog_end;
    r.skirt_drop = conf->skirt_drop;
    r.fog_color[0] = conf->fog_color[0];
    r.fog_color[1] = conf->fog_color[1];
    r.fog_color[2] = conf->fog_color[2];
    r.fog_color[3] = conf->fog_color[3];
    r.ambient_light[0] = conf->ambient_light[0];
    r.ambient_light[1] = conf->ambient_light[1];
    r.ambient_light[2] = conf->ambient_light[2];
    r.ambient_light[3] = conf->ambient_light[3];
    r.sun_direction[0] = conf->sun_direction[0];
    r.sun_direction[1] = conf->sun_direction[1];
    r.sun_direction[2] = conf->sun_direction[2];
    r.sun_scale = conf->sun_scale;
    r.height_scale = conf->height_scale;
    r.normals_scale = conf->normals_scale;

    raytiles::pool_config p{};
    p.download_threads = pool_conf->download_threads;
    p.allow_insecure_tls = pool_conf->allow_insecure_tls;
    p.use_logger = pool_conf->use_logger;
    p.texture_cache_path = to_string_or_empty(pool_conf->texture_cache_path);
    p.heightmap_cache_path = to_string_or_empty(pool_conf->heightmap_cache_path);
    p.normals_cache_path = to_string_or_empty(pool_conf->normals_cache_path);
    p.texture_host = to_string_or_empty(pool_conf->texture_host);
    p.texture_url_path = to_string_or_empty(pool_conf->texture_url_path);
    p.heightmap_host = to_string_or_empty(pool_conf->heightmap_host);
    p.heightmap_url_path = to_string_or_empty(pool_conf->heightmap_url_path);
    p.normals_host = to_string_or_empty(pool_conf->normals_host);
    p.normals_url_path = to_string_or_empty(pool_conf->normals_url_path);

    try {
        return new RaytilesStreamer(w, s, r, p);
    } catch (...) {
        return nullptr;
    }
}

void RaytilesStreamerDestroy(const RaytilesStreamer *streamer) {
    delete streamer;
}

void RaytilesStreamerUpdate(RaytilesStreamer *streamer, Camera3D camera) {
    if (!streamer) return;
    streamer->impl.update(camera);
}

void RaytilesStreamerDraw(RaytilesStreamer *streamer, Camera3D camera) {
    if (!streamer) return;
    streamer->impl.draw(camera);
}

void RaytilesStreamerDebug(RaytilesStreamer *streamer, Camera3D camera) {
    if (!streamer) return;
    streamer->impl.debug(camera);
}

void RaytilesStreamerDebug3D(RaytilesStreamer *streamer) {
    if (!streamer) return;
    streamer->impl.debug_3d();
}

bool RaytilesStreamerGroundHeight(RaytilesStreamer *streamer,
                                  const Vector3 position,
                                  float *out_height) {
    if (!streamer) return false;
    const auto h = streamer->impl.ground_height(position);
    if (!h.has_value()) return false;
    if (out_height) *out_height = *h;
    return true;
}

void RaytilesStreamerSetAmbientLight(RaytilesStreamer *streamer, const Color color) {
    if (!streamer) return;
    streamer->impl.get_renderer().set_ambient_light(color);
}

void RaytilesStreamerSetAmbientLightV4(RaytilesStreamer *streamer, const Vector4 color) {
    if (!streamer) return;
    streamer->impl.get_renderer().set_ambient_light(color);
}

void RaytilesStreamerSetAmbientLightRGBA(RaytilesStreamer *streamer,
                                         const float r, const float g, const float b, const float a) {
    if (!streamer) return;
    streamer->impl.get_renderer().set_ambient_light(r, g, b, a);
}

void RaytilesStreamerSetFogColor(RaytilesStreamer *streamer, const Color color) {
    if (!streamer) return;
    streamer->impl.get_renderer().set_fog_color(color);
}

void RaytilesStreamerSetFogColorV4(RaytilesStreamer *streamer, const Vector4 color) {
    if (!streamer) return;
    streamer->impl.get_renderer().set_fog_color(color);
}

void RaytilesStreamerSetFogColorRGBA(RaytilesStreamer *streamer,
                                     const float r, const float g, const float b, const float a) {
    if (!streamer) return;
    streamer->impl.get_renderer().set_fog_color(r, g, b, a);
}

void RaytilesStreamerSetFogStart(RaytilesStreamer *streamer, const float distance) {
    if (!streamer) return;
    streamer->impl.get_renderer().set_fog_start(distance);
}

void RaytilesStreamerSetFogEnd(RaytilesStreamer *streamer, const float distance) {
    if (!streamer) return;
    streamer->impl.get_renderer().set_fog_end(distance);
}

void RaytilesStreamerSetHeightScale(RaytilesStreamer *streamer, const float scale) {
    if (!streamer) return;
    streamer->impl.get_renderer().set_height_scale(scale);
}

void RaytilesStreamerSetNormalsScale(RaytilesStreamer *streamer, const float scale) {
    if (!streamer) return;
    streamer->impl.get_renderer().set_normals_scale(scale);
}

void RaytilesStreamerSetSunDirection(RaytilesStreamer *streamer, const Vector3 direction) {
    if (!streamer) return;
    streamer->impl.get_renderer().set_sun_direction(direction);
}

void RaytilesStreamerSetSunScale(RaytilesStreamer *streamer, const float scale) {
    if (!streamer) return;
    streamer->impl.get_renderer().set_sun_scale(scale);
}
} // extern "C"
