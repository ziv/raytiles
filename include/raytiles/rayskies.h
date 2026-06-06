#pragma once
#include <memory>
#include <raylib.h>


namespace raytiles::sky {
    // todo add time
    // todo add ambient color
    // todo add cloud params
    struct sky_config {
        float zenith_color[3] = {0.1f, 0.01f, 0.6f};
        float horizon_color[3] = {0.3f, 0.7f, 0.88f};
    };

    class sky_renderer;

    class sky_steamer {
    public:
        explicit sky_steamer(const sky_config &config = {});

        ~sky_steamer();

        void draw(Vector3 player_pos) const;

        void set_horizon_color(float r, float g, float b) const;

        void set_horizon_color(Color color) const;

        void set_zenith_color(float r, float g, float b) const;

        void set_zenith_color(Color color) const;

    private:
        std::unique_ptr<sky_renderer> renderer;
    };
}
