#pragma once

#include <future>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../raytiles.h"
#include "downloader.hpp"
#include "raii.hpp"
#include "raylib.h"
#include "tilekey.hpp"

namespace raytiles {
    struct loading_tile {
        float tx;
        float tz;
        std::shared_future<std::string> tx_future;
        std::shared_future<std::string> hm_future;
        std::shared_future<std::string> nl_future;
    };

    struct loaded_tile {
        float tx;
        float tz;
        raii::texture tx_texture;
        raii::texture hm_texture;
        raii::image hm_image;
        raii::texture nl_texture;
    };

    class manager {
    public:
        manager(const config &conf, pool_config pool_conf);

        void update(const Camera3D &camera);

        void draw(const Camera3D &camera);

        void debug(const Camera3D &camera);

        void debug_3d(const Camera3D &camera);

        void set_ambient_light(Color color);

        void set_ambient_light(Vector4 color);

        void set_ambient_light(float r, float g, float b, float a);

        void set_fog_color(Color color);
        void set_fog_color(Vector4 color);
        void set_fog_color(float r, float g, float b, float a);

        void set_fog_start(float distance);

        void set_fog_end(float distance);

        void set_sun_direction(Vector3 direction);

        void set_sun_scale(float scale);

        void set_height_scale(float scale);

        void set_normals_scale(float scale);

        [[nodiscard]] std::optional<float> ground_height(Vector3 position) const;

    private:
        void process_loaded_tiles();

        void process_current_location();

        void remove_unused_tiles();

        void update_shader_uniforms();

        loading_tile spawn(const TileKey &tile);

        [[nodiscard]] bool is_tile_covered(const TileKey &key) const;

        [[nodiscard]] bool is_tile_out_of_area(const TileKey &key) const;

        config conf;
        raii::shader displacement_shader;
        pool tile_downloader;

        int cam_pos_loc = -1;
        int ambient_loc = -1;
        int fog_color_log = -1;
        int tex_albedo_loc = -1;
        int tex_height_loc = -1;
        int tex_normal_loc = -1;
        int sun_dir_loc = -1;
        int sun_scale_loc = -1;
        int height_scale_loc = -1;
        int normal_scale_loc = -1;
        int fog_start_loc = -1;
        int fog_end_loc = -1;

        float ambient_light[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float fog_color[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        float sun_direction[3] = {0.1f, 1.0f, 0.1f};
        float fog_start = 1.0f;
        float fog_end = 100000.0f;
        float height_scale = 1.0f;
        float normals_scale = 1.0f;
        float sun_scale = 1.0f;

        std::vector<raii::model> models = {};
        std::vector<float> tile_sizes = {};
        std::vector<float> tile_distances = {};

        Vector3 last_position = {-9999.9f, -9999.9f, -9999.9f};

        std::unordered_set<TileKey> desired_keys;
        std::unordered_map<TileKey, loading_tile> loading_tiles;
        std::unordered_map<TileKey, loaded_tile> rendering_tiles;
    };
} // namespace raytiles
