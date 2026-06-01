#pragma once
#include "detail/utils.hpp"

namespace raytiles {
    struct sky_shader_options {
        // todo add options for sky color, sun position, cloud parameters, etc
        float zenithColor[4] = {0.0f, 0.5f, 1.0f, 1.0f};
        float horizonColor[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    };

    class sky_shader {
    public:
        explicit sky_shader(const sky_shader_options &opts = {});

        /// Returns the underlying raylib `Shader` handle. Use this when you
        /// need to bind the shader to a `Material` or pass it to a raylib
        /// drawing call directly.
        const Shader &operator()() const noexcept { return *shader; }

        sky_shader &set_zenith_color(float r, float g, float b, float a);

        sky_shader &set_horizon_color(float r, float g, float b, float a);

        sky_shader &set_time(float time);

    private:
        sky_shader_options options;

        int zenith_color_loc = -1;
        int horizon_color_loc = -1;
        int time_loc = -1;

        raii::shader shader;
    };
}
