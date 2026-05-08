#include "../raytiles.h"
#if defined(_WIN32)
#define NOGDI
#define NOUSER
#endif
#include <raylib.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "manager.hpp"
#include "rlgl.h"
#include "tilekey.hpp"

using namespace std::chrono_literals;

namespace raytiles {
    namespace {
        /// a better performance function instead of using
        /// const auto c = GetImageColor(img, px, pz);
        float get_height_from_image(const Image &img, int x, int y) {
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x >= img.width) x = img.width - 1;
            if (y >= img.height) y = img.height - 1;

            const auto pixels = static_cast<const unsigned char *>(img.data);
            unsigned char r, g, b;

            if (img.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8) {
                const int index = (y * img.width + x) * 3;
                r = pixels[index];
                g = pixels[index + 1];
                b = pixels[index + 2];
            } else if (img.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
                const int index = (y * img.width + x) * 4;
                r = pixels[index];
                g = pixels[index + 1];
                b = pixels[index + 2];
            } else {
                return 0.0f;
            }

            return -10000.0f + (static_cast<float>(r) * 65536.0f + static_cast<float>(g) * 256.0f + static_cast<float>(b)) * 0.1f;
        }

        int radius(const float height) {
            if (height < 500) return 2;
            if (height < 1000) return 3;
            if (height < 2000) return 4;
            if (height < 4000) return 5;
            if (height < 8000) return 6;
            return 7;
        }

#ifdef __EMSCRIPTEN__
#define GLSL_VERSION_HEADER "#version 300 es\nprecision mediump float;\n"
#else
#define GLSL_VERSION_HEADER "#version 330\n"
#endif

        constexpr auto vertex_shader = GLSL_VERSION_HEADER R"(
in vec3 vertexPosition;
in vec2 vertexTexCoord;

uniform mat4 mvp;
uniform mat4 matModel;
uniform sampler2D heightMap;
uniform float heightScale;
uniform vec3 cameraPosition;
uniform float skirtDrop;

out vec2 fragTexCoord;
out float fragCamDist;

void main()
{
    fragTexCoord = vertexTexCoord;

    vec3 color = texture(heightMap, vertexTexCoord).rgb;
    vec3 c = color * 255.0;

    // Terrarium heightmap calculations
    float heightValue = (c.r * 256.0 + c.g + c.b / 256.0) - 32768.0;

    vec3 displacedPosition = vertexPosition;
    displacedPosition.y += heightValue * heightScale;


    // temporary disabled
    // add skirt to the edges of the terrain to hide gaps between tiles
    // float epsilon = 0.001;
    // bool isEdge = (vertexTexCoord.x < epsilon) ||
    //               (vertexTexCoord.x > 1.0 - epsilon) ||
    //               (vertexTexCoord.y < epsilon) ||
    //              (vertexTexCoord.y > 1.0 - epsilon);
    // if (isEdge)
    // {
    //     displacedPosition.y -= skirtDrop * heightScale;
    // }

    vec3 worldPosition = vec3(matModel * vec4(displacedPosition, 1.0));
    fragCamDist = distance(worldPosition, cameraPosition);

    gl_Position = mvp * vec4(displacedPosition, 1.0);
}
)";

        constexpr auto fragment_shader = GLSL_VERSION_HEADER R"(
in vec2 fragTexCoord;
in float fragCamDist;

uniform sampler2D texture0;
uniform vec4 ambientLight;
uniform vec4 fogColor;
uniform float fogStart;
uniform float fogEnd;

out vec4 finalColor;

void main()
{
    vec4 texColor = texture(texture0, fragTexCoord);
    vec4 lit = texColor * ambientLight;
    float fogFactor = clamp((fragCamDist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    finalColor = mix(lit, fogColor, fogFactor);
}
)";
    } // namespace

    // manager

    manager::manager(config conf, pool_config pool_conf)
        : conf(std::move(conf)),
          displacement_shader(raii::load_shader_from_memory(vertex_shader, fragment_shader)),
          tile_downloader(std::move(pool_conf)) {
        // set the rendering distance
        rlSetClipPlanes(conf.near_plane, conf.far_plane);
        desired_keys.reserve(512);
        const auto distance = static_cast<float>(conf.rendering_radius) * conf.base_zoom_tile_size;

        // prepare tiles size and distance for each zoom
        const int zoom_count = conf.max_zoom - conf.base_zoom + 1;
        tile_sizes.resize(zoom_count);
        tile_distances.resize(zoom_count);
        for (int zoom = conf.base_zoom; zoom <= conf.max_zoom; ++zoom) {
            const int ratio = 1 << (zoom - conf.base_zoom);
            const int idx = zoom - conf.base_zoom;
            tile_sizes[idx] = conf.base_zoom_tile_size / static_cast<float>(ratio);
            tile_distances[idx] = distance * distance / static_cast<float>(ratio * ratio * ratio);
        }

        // creating model for each zoom level, avoiding the need to stretch the model
        models.reserve(zoom_count);
        for (int zoom = conf.base_zoom; zoom <= conf.max_zoom; ++zoom) {
            const int idx = zoom - conf.base_zoom;
            const int res = 16 * (1 << idx);
            const float size = tile_sizes[idx];

            float skirt_size = conf.skirt_size;
            if (zoom == 13) skirt_size /= 2;
            if (zoom == 12) skirt_size /= 4;
            if (zoom == 11) skirt_size /= 8;

            models.emplace_back(raii::load_model_from_mesh(GenMeshPlane(size + skirt_size, size + skirt_size, res, res)));
            models[idx]->materials[0].shader = *displacement_shader;
        }

        // cache shader locations
        cam_pos_loc = GetShaderLocation(*displacement_shader, "cameraPosition");
        ambient_loc = GetShaderLocation(*displacement_shader, "ambientLight");
        fog_color_log = GetShaderLocation(*displacement_shader, "fogColor");

        // configure the slot the heightmap data use - MATERIAL_MAP_ROUGHNESS slot
        constexpr int heightmapSlotIndex = MATERIAL_MAP_ROUGHNESS;
        SetShaderValue(*displacement_shader, GetShaderLocation(*displacement_shader, "heightMap"), &heightmapSlotIndex, SHADER_UNIFORM_INT);

        // todo cache keys and add to "update_shader_uniforms()" too allow change those values based on camera y position
        SetShaderValue(*displacement_shader, GetShaderLocation(*displacement_shader, "heightScale"), &conf.height_scale, SHADER_UNIFORM_FLOAT);
        SetShaderValue(*displacement_shader, GetShaderLocation(*displacement_shader, "fogStart"), &conf.fog_start, SHADER_UNIFORM_FLOAT);
        SetShaderValue(*displacement_shader, GetShaderLocation(*displacement_shader, "fogEnd"), &conf.fog_end, SHADER_UNIFORM_FLOAT);
        SetShaderValue(*displacement_shader, GetShaderLocation(*displacement_shader, "skirtDrop"), &conf.skirt_drop, SHADER_UNIFORM_FLOAT);

        // the reset shaders uniform (those are dynamically changed...)
        update_shader_uniforms();

        if (conf.use_logger) TraceLog(LOG_INFO, "raytiles streamer initialized");
    }

    void manager::update(const Camera3D &camera) {
        // start with clearing items we've done with
        remove_unused_tiles();

        const auto position = camera.position;

        // look for done futures in "desired_tiles" to build "rendered_tiles" map
        process_loaded_tiles();

        // do not update tiles list if didn't pass enough distance
        if (Vector3DistanceSqr(position, last_position) < conf.update_distance && std::fabs(position.y - last_position.y) < conf.update_height) return;
        last_position = position;

        // use current location to build "desired_tiles" map
        process_current_location();
    }

    void manager::draw(const Camera3D &camera) {
        // set the camera location (for distance -> fog)
        SetShaderValue(*displacement_shader, cam_pos_loc, &camera.position, SHADER_UNIFORM_VEC3);

        // horizontal forward direction (xz plane). tiles are flat on y=0 so testing
        // against the horizontal projection of the camera forward is enough to decide
        // "is the tile in front of (or beside) the camera?". if the camera is looking
        // straight up/down, fwd_len ~ 0 and we disable culling for this frame.
        const float fwd_x = camera.target.x - camera.position.x;
        const float fwd_z = camera.target.z - camera.position.z;
        const float fwd_len = std::sqrt(fwd_x * fwd_x + fwd_z * fwd_z);
        const bool cull_enabled = fwd_len > 0.0001f;
        const float fwd_nx = cull_enabled ? fwd_x / fwd_len : 0.0f;
        const float fwd_nz = cull_enabled ? fwd_z / fwd_len : 0.0f;

        for (const auto &[key, tile]: rendering_tiles) {
            const auto size = tile_sizes[key.zoom - conf.base_zoom];

            // skip tiles that lie behind the camera by more than one tile-size buffer.
            // tiles to the side (perpendicular to forward) and anything in front pass.
            // the buffer is the tile size at this zoom so a tile straddling the camera
            // plane is still drawn.
            if (cull_enabled) {
                const float dx = tile.tx - camera.position.x;
                const float dz = tile.tz - camera.position.z;
                const float along_forward = dx * fwd_nx + dz * fwd_nz;
                if (along_forward < -size) continue;
            }

            const auto &model = models[key.zoom - conf.base_zoom];
            model->materials[0].maps[MATERIAL_MAP_ALBEDO].texture = *tile.tx_texture;
            model->materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture = *tile.hm_texture;

            DrawModel(*model, {tile.tx, 0.0f, tile.tz}, 1.0f, WHITE);
        }
    }

    void manager::debug_3d(const Camera3D &camera) {
        // see manager::draw
        const float fwd_x = camera.target.x - camera.position.x;
        const float fwd_z = camera.target.z - camera.position.z;
        const float fwd_len = std::sqrt(fwd_x * fwd_x + fwd_z * fwd_z);
        const bool cull_enabled = fwd_len > 0.0001f;
        const float fwd_nx = cull_enabled ? fwd_x / fwd_len : 0.0f;
        const float fwd_nz = cull_enabled ? fwd_z / fwd_len : 0.0f;

        for (const auto &[key, tile]: rendering_tiles) {
            const auto size = tile_sizes[key.zoom - conf.base_zoom];
            if (cull_enabled) {
                const float dx = tile.tx - camera.position.x;
                const float dz = tile.tz - camera.position.z;
                const float along_forward = dx * fwd_nx + dz * fwd_nz;
                if (along_forward < -size) continue;
            }
            DrawCubeWires({tile.tx, 0.0f, tile.tz}, size, 200.0f, size, RED);
        }
    }

    void manager::debug(const Camera3D &camera) {
        const auto width = GetScreenWidth();
        const auto height = GetScreenHeight();
        for (const auto &[key, tile]: rendering_tiles) {
            const auto [x, y] = GetWorldToScreen({tile.tx, 0.0f, tile.tz}, camera);
            if (x < 0 || x > width || y < 0 || y > height) continue;

            const Color c = key.zoom == 14 ? RED : key.zoom == 15 ? GREEN : WHITE;
            DrawText(TextFormat("%d", key.zoom), static_cast<int>(x), static_cast<int>(y), 15, c);
        }
    }

    void manager::remove_unused_tiles() {
        std::erase_if(rendering_tiles, [&](const auto &item) {
            if (desired_keys.contains(item.first)) return false;
            if (!is_tile_covered(item.first)) return false;
            return true;
        });
        // also drop loading-tile bookkeeping for tiles we no longer want. the
        // background workers still finish their downloads and write to disk (the
        // file cache is the whole point of background streaming), but we stop
        // holding the resolved bytes in memory once they arrive.
        std::erase_if(loading_tiles, [&](const auto &item) { return !desired_keys.contains(item.first); });
    }

    void manager::set_ambient_light(const Color color) {
        ambient_light[0] = static_cast<float>(color.r) / 255.0f;
        ambient_light[1] = static_cast<float>(color.g) / 255.0f;
        ambient_light[2] = static_cast<float>(color.b) / 255.0f;
        ambient_light[3] = static_cast<float>(color.a) / 255.0f;

        update_shader_uniforms();
    }

    void manager::set_fog_color(const Color color) {
        fog_color[0] = static_cast<float>(color.r) / 255.0f;
        fog_color[1] = static_cast<float>(color.g) / 255.0f;
        fog_color[2] = static_cast<float>(color.b) / 255.0f;
        fog_color[3] = static_cast<float>(color.a) / 255.0f;

        update_shader_uniforms();
    }

    std::optional<float> manager::ground_height(const Vector3 position) const {
        // walk from the highest available zoom down to base; whichever zoom holds the
        // tile that contains (position.x, position.z) wins. higher zoom = finer
        // sample, so we prefer it if loaded.
        for (int zoom = conf.max_zoom; zoom >= conf.base_zoom; --zoom) {
            const float size = tile_sizes[zoom - conf.base_zoom];

            const int tile_x = static_cast<int>(std::floor(position.x / size));
            const int tile_z = static_cast<int>(std::floor(position.z / size));

            const auto it = rendering_tiles.find(TileKey{zoom, tile_x, tile_z});
            if (it == rendering_tiles.end()) continue;

            const auto &tile = it->second;
            const Image &img = *tile.hm_image;
            if (!IsImageValid(img)) continue;

            // local uv inside the tile, [0, 1)
            const float u = (position.x - static_cast<float>(tile_x) * size) / size;
            const float v = (position.z - static_cast<float>(tile_z) * size) / size;

            const int px = static_cast<int>(u * static_cast<float>(img.width));
            const int py = static_cast<int>(v * static_cast<float>(img.height));

            return get_height_from_image(img, px, py);
        }
        return std::nullopt;
    }

    void manager::process_current_location() {
        desired_keys.clear();

        auto subdivide = [&](auto &self, const int zoom, const int tx, const int tz) -> void {
            // no need for calculations, this is the last zoom
            if (zoom == conf.max_zoom) {
                desired_keys.insert({zoom, tx, tz});
                return;
            }

            // calculate distance of the tile from the camera
            const float tile_size = tile_sizes[zoom - conf.base_zoom];
            const float world_x = (static_cast<float>(tx) + 0.5f) * tile_size;
            const float world_z = (static_cast<float>(tz) + 0.5f) * tile_size;
            const float ddx = last_position.x - world_x;
            const float ddz = last_position.z - world_z;

            // check against the next zoom distance threshold, if it's far enough,
            // add to the list, otherwise subdivide into 4 children
            if (ddx * ddx + ddz * ddz >= tile_distances[zoom + 1 - conf.base_zoom]) {
                desired_keys.insert({zoom, tx, tz});
                return;
            }

            // split into 4 children at zoom+1.
            const int child_zoom = zoom + 1;
            const int cx0 = tx * 2;
            const int cz0 = tz * 2;
            for (int ox = 0; ox < 2; ++ox)
                for (int oz = 0; oz < 2; ++oz) self(self, child_zoom, cx0 + ox, cz0 + oz);
        };

        const int current_tile_x = static_cast<int>(std::floor(last_position.x / conf.base_zoom_tile_size));
        const int current_tile_z = static_cast<int>(std::floor(last_position.z / conf.base_zoom_tile_size));

        const auto r = radius(last_position.y);
        const auto allowed_radius = (r - 1) * (r - 1);

        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz)
                if (dz * dz + dx * dx < allowed_radius) subdivide(subdivide, conf.base_zoom, current_tile_x + dx, current_tile_z + dz);

        // spawn new if not in rendering list
        for (const auto &key: desired_keys) {
            if (!rendering_tiles.contains(key) && !loading_tiles.contains(key)) loading_tiles.try_emplace(key, spawn(key));
        }
    }

    void manager::process_loaded_tiles() {
        // walk loading tiles; for each entry that finished downloading, either promote
        // it to rendering_tiles or drop it (invalid / no longer desired). entries are
        // erased immediately on the iterator. uploads are bounded both by a wall-clock
        // budget (to keep the frame steady on slow GPUs) and a hard cap (to keep heavy
        // single-tile uploads from running away).
        const double frame_start = GetTime();
        int promoted = 0;

        for (auto it = loading_tiles.begin(); it != loading_tiles.end();) {
            auto &[key, tile] = *it;

            // both byte futures must be ready
            if (tile.tx_future.wait_for(0s) != std::future_status::ready || tile.hm_future.wait_for(0s) != std::future_status::ready) {
                ++it;
                continue;
            }

            // futures resolved with raw file bytes - decode here on the main thread to
            // keep raylib calls off the worker threads. take const refs since the
            // shared_future may be reused by multiple consumers (we don't move-out).
            // a worker exception (network failure, fopen, etc.) propagates through
            // .get(); drop the tile so the rest of the world keeps streaming.
            const std::string *tx_bytes_ptr = nullptr;
            const std::string *hm_bytes_ptr = nullptr;
            try {
                tx_bytes_ptr = &tile.tx_future.get();
                hm_bytes_ptr = &tile.hm_future.get();
                if (conf.use_logger) TraceLog(LOG_DEBUG, "tile %d/%d/%d loaded", key.zoom, key.x, key.z);
            } catch (const std::exception &e) {
                TraceLog(LOG_WARNING, "tile %d/%d/%d download failed: %s - dropping", key.zoom, key.x, key.z, e.what());
                it = loading_tiles.erase(it);
                continue;
            }
            const std::string &tx_bytes = *tx_bytes_ptr;
            const std::string &hm_bytes = *hm_bytes_ptr;

            // take ownership of the decoded images via RAII; they unload automatically
            // on every exit path below.
            raii::image tex_img{LoadImageFromMemory(".png", reinterpret_cast<const unsigned char *>(tx_bytes.data()), static_cast<int>(tx_bytes.size()))};
            raii::image height_img{LoadImageFromMemory(".png", reinterpret_cast<const unsigned char *>(hm_bytes.data()), static_cast<int>(hm_bytes.size()))};

            if (!IsImageValid(*tex_img) || !IsImageValid(*height_img)) {
                TraceLog(LOG_WARNING, "failed to load tile %d/%d/%d - dropping", key.zoom, key.x, key.z);
                it = loading_tiles.erase(it);
                continue;
            }

            if (!desired_keys.contains(key)) {
                if (conf.use_logger) TraceLog(LOG_DEBUG, "tile %d/%d/%d became stale before upload - dropping", key.zoom, key.x, key.z);
                it = loading_tiles.erase(it);
                continue;
            }

            // ensure the heightmap image is in a format get_height_from_image() can
            // decode. raylib normally gives us R8G8B8 / R8G8B8A8, but a future raylib
            // upgrade or a custom-cached image could come in something else; convert
            // once here so ground_height() is never silently wrong.
            if (height_img->format != PIXELFORMAT_UNCOMPRESSED_R8G8B8 && height_img->format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
                TraceLog(LOG_WARNING, "heightmap tile %d/%d/%d arrived as format %d - converting to R8G8B8", key.zoom, key.x, key.z, height_img->format);
                ImageFormat(&height_img.get(), PIXELFORMAT_UNCOMPRESSED_R8G8B8);
            }

            // upload to GPU and move into rendering_tiles. the heightmap CPU image is
            // kept in the loaded_tile for ground_height() queries (recast, collision).
            raii::texture texture_tex = raii::load_texture_from_image(*tex_img);
            raii::texture height_tex = raii::load_texture_from_image(*height_img);
            SetTextureWrap(*texture_tex, TEXTURE_WRAP_CLAMP);
            SetTextureWrap(*height_tex, TEXTURE_WRAP_CLAMP);

            if (conf.use_mipmap) {
                GenTextureMipmaps(&texture_tex.get());
                SetTextureFilter(*texture_tex, TEXTURE_FILTER_ANISOTROPIC_16X);
            }

            rendering_tiles.insert_or_assign(key, loaded_tile{tile.tx, tile.tz, std::move(texture_tex), std::move(height_tex), std::move(height_img)});

            it = loading_tiles.erase(it);
            ++promoted;

            if (promoted >= conf.max_uploads_per_frame) break;
            if (GetTime() - frame_start >= conf.upload_budget_sec) break;
        }
    }

    void manager::update_shader_uniforms() {
        // set the ambient color (weather/day/night/...)
        SetShaderValue(*displacement_shader, ambient_loc, ambient_light, SHADER_UNIFORM_VEC4);

        // set the fog color (to match the sky)
        SetShaderValue(*displacement_shader, fog_color_log, fog_color, SHADER_UNIFORM_VEC4);
    }

    loading_tile manager::spawn(const TileKey &tile) {
        const auto scale = 1 << (tile.zoom - conf.base_zoom);
        const auto tx = tile.x + conf.anchor_x_tile * scale;
        const auto tz = tile.z + conf.anchor_z_tile * scale;
        const auto tile_size = tile_sizes[tile.zoom - conf.base_zoom];

        auto t = loading_tile{
            // loading tile structure
            (static_cast<float>(tile.x) + 0.5f) * tile_size,
            (static_cast<float>(tile.z) + 0.5f) * tile_size,
            tile_downloader.enqueue_texture(tile.zoom, tx, tz),
            tile_downloader.enqueue_heightmap(tile.zoom, tx, tz)
        };

        if (conf.use_logger) TraceLog(LOG_DEBUG, "spawned tile %d,%d,%d position %f,%f", tile.zoom, tile.x, tile.z, t.tx, t.tz);
        return t;
    }

    bool manager::is_tile_covered(const TileKey &key) const {
        const auto contains = [&](const int zoom, const int x, const int z) { return rendering_tiles.contains(TileKey{zoom, x, z}); };

        // check parent
        if (key.zoom > conf.base_zoom) {
            if (contains(key.zoom - 1, key.x >> 1, key.z >> 1)) return true;
        }

        // check children. use multiplication instead of left-shift: signed left-shift
        // on negative tile indices (which we have around the anchor) is UB even in
        // C++20.
        if (key.zoom < conf.max_zoom) {
            const int child_x = key.x * 2;
            const int child_z = key.z * 2;
            if (contains(key.zoom + 1, child_x, child_z) && contains(key.zoom + 1, child_x + 1, child_z) && contains(key.zoom + 1, child_x, child_z + 1) &&
                contains(key.zoom + 1, child_x + 1, child_z + 1)) {
                return true;
            }
        }
        return false;
    }

    // streamer (pImpl forwarding)

    streamer::streamer(config conf, pool_config pool_conf) : impl(
        std::make_unique<manager>(std::move(conf), std::move(pool_conf))) {
    }

    streamer::~streamer() = default;

    streamer::streamer(streamer &&) noexcept = default;

    streamer &streamer::operator=(streamer &&) noexcept = default;

    void streamer::update(const Camera3D &camera) const { impl->update(camera); }
    void streamer::draw(const Camera3D &camera) const { impl->draw(camera); }
    void streamer::debug(const Camera3D &camera) const { impl->debug(camera); }
    void streamer::debug_3d(const Camera3D &camera) const { impl->debug_3d(camera); }
    void streamer::set_ambient_light(const Color color) const { impl->set_ambient_light(color); }
    void streamer::set_fog_color(const Color color) const { impl->set_fog_color(color); }
    std::optional<float> streamer::ground_height(const Vector3 position) const { return impl->ground_height(position); }
} // namespace raytiles
