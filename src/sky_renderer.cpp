#include "detail/sky_renderer.h"

namespace rayties {

    sky_renderer::sky_renderer(const sky_shader_options &opts) : shader_(opts) {
        // material = raii::material{LoadMaterialDefault()};
        // material->shader = shader_();
    }
}