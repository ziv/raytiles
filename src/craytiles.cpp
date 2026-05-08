/// @file craytiles.cpp
/// Implementation of the C wrapper declared in craytiles.h.
/// Translates C structs / opaque handles into raytiles::streamer calls.
#include "../craytiles.h"

#include <new>
#include <string>
#include <utility>

#include "../raytiles.h"

namespace {
    inline std::string to_string_or_empty(const char *s) {
        return s ? std::string(s) : std::string();
    }
}

struct RaytilesStreamer {
    raytiles::streamer impl;

    RaytilesStreamer(raytiles::config c, raytiles::pool_config p)
        : impl(std::move(c), std::move(p)) {}
};

extern "C" {

RaytilesConfig RaytilesConfigDefault(void) {
    constexpr raytiles::config d{};
    RaytilesConfig out{};
    out.base_zoom             = d.base_zoom;
    out.max_zoom              = d.max_zoom;
    out.base_zoom_tile_size   = d.base_zoom_tile_size;
    out.rendering_radius      = d.rendering_radius;
    out.skirt_size            = d.skirt_size;
    out.height_scale          = d.height_scale;
    out.update_distance       = d.update_distance;
    out.update_height         = d.update_height;
    out.upload_budget_sec     = d.upload_budget_sec;
    out.max_uploads_per_frame = d.max_uploads_per_frame;
    out.anchor_x_tile         = d.anchor_x_tile;
    out.anchor_z_tile         = d.anchor_z_tile;
    out.near_plane            = d.near_plane;
    out.far_plane             = d.far_plane;
    out.use_mipmap            = d.use_mipmap;
    out.skirt_drop            = d.skirt_drop;
    out.fog_start             = d.fog_start;
    out.fog_end               = d.fog_end;
    out.use_logger            = d.use_logger;
    return out;
}

RaytilesPoolConfig RaytilesPoolConfigDefault(void) {
    static const raytiles::pool_config d{};
    RaytilesPoolConfig out{};
    out.download_threads     = d.download_threads;
    out.allow_insecure_tls   = d.allow_insecure_tls;
    out.use_logger           = d.use_logger;
    out.texture_cache_path   = d.texture_cache_path.c_str();
    out.heightmap_cache_path = d.heightmap_cache_path.c_str();
    out.texture_host         = d.texture_host.c_str();
    out.texture_url_path     = d.texture_url_path.c_str();
    out.heightmap_host       = d.heightmap_host.c_str();
    out.heightmap_url_path   = d.heightmap_url_path.c_str();
    return out;
}

RaytilesStreamer *RaytilesStreamerCreate(const RaytilesConfig     *conf,
                                         const RaytilesPoolConfig *pool_conf) {
    if (!conf || !pool_conf) return nullptr;

    raytiles::config c{};
    c.base_zoom             = conf->base_zoom;
    c.max_zoom              = conf->max_zoom;
    c.base_zoom_tile_size   = conf->base_zoom_tile_size;
    c.rendering_radius      = conf->rendering_radius;
    c.skirt_size            = conf->skirt_size;
    c.height_scale          = conf->height_scale;
    c.update_distance       = conf->update_distance;
    c.update_height         = conf->update_height;
    c.upload_budget_sec     = conf->upload_budget_sec;
    c.max_uploads_per_frame = conf->max_uploads_per_frame;
    c.anchor_x_tile         = conf->anchor_x_tile;
    c.anchor_z_tile         = conf->anchor_z_tile;
    c.near_plane            = conf->near_plane;
    c.far_plane             = conf->far_plane;
    c.use_mipmap            = conf->use_mipmap;
    c.skirt_drop            = conf->skirt_drop;
    c.fog_start             = conf->fog_start;
    c.fog_end               = conf->fog_end;
    c.use_logger            = conf->use_logger;

    raytiles::pool_config p{};
    p.download_threads     = pool_conf->download_threads;
    p.allow_insecure_tls   = pool_conf->allow_insecure_tls;
    p.use_logger           = pool_conf->use_logger;
    p.texture_cache_path   = to_string_or_empty(pool_conf->texture_cache_path);
    p.heightmap_cache_path = to_string_or_empty(pool_conf->heightmap_cache_path);
    p.texture_host         = to_string_or_empty(pool_conf->texture_host);
    p.texture_url_path     = to_string_or_empty(pool_conf->texture_url_path);
    p.heightmap_host       = to_string_or_empty(pool_conf->heightmap_host);
    p.heightmap_url_path   = to_string_or_empty(pool_conf->heightmap_url_path);

    try {
        return new RaytilesStreamer(std::move(c), std::move(p));
    } catch (...) {
        return nullptr;
    }
}

void RaytilesStreamerDestroy(const RaytilesStreamer *streamer) {
    delete streamer;
}

void RaytilesStreamerUpdate(const RaytilesStreamer *streamer, const Camera3D &camera) {
    if (!streamer) return;
    streamer->impl.update(camera);
}

void RaytilesStreamerDraw(const RaytilesStreamer *streamer, const Camera3D &camera) {
    if (!streamer) return;
    streamer->impl.draw(camera);
}

void RaytilesStreamerDebug(const RaytilesStreamer *streamer, const Camera3D &camera) {
    if (!streamer) return;
    streamer->impl.debug(camera);
}

void RaytilesStreamerDebug3D(const RaytilesStreamer *streamer, const Camera3D &camera) {
    if (!streamer) return;
    streamer->impl.debug_3d(camera);
}

void RaytilesStreamerSetAmbientLight(const RaytilesStreamer *streamer, const Color color) {
    if (!streamer) return;
    streamer->impl.set_ambient_light(color);
}

void RaytilesStreamerSetFogColor(const RaytilesStreamer *streamer, const Color color) {
    if (!streamer) return;
    streamer->impl.set_fog_color(color);
}

bool RaytilesStreamerGroundHeight(const RaytilesStreamer *streamer,
                                  const Vector3 position,
                                  float  *out_height) {
    if (!streamer) return false;
    const auto h = streamer->impl.ground_height(position);
    if (!h.has_value()) return false;
    if (out_height) *out_height = *h;
    return true;
}

} // extern "C"
