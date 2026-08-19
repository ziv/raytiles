#include "detail/sky_renderer.h"

#include <raylib.h>
#include <rlgl.h>

namespace raytiles::sky {
namespace {
raii::model load_sky_model() {
  const Mesh sky_mesh = GenMeshSphere(1000.0f, 16, 16);
  return raii::model(LoadModelFromMesh(sky_mesh));
}
}  // namespace

sky_renderer::sky_renderer(const sky_config& config) : shader(config), sky_model(load_sky_model()) { sky_model->materials[0].shader = shader(); }

void sky_renderer::draw(const Vector3 player_pos) {
  rlDisableDepthTest();
  rlDisableBackfaceCulling();
  DrawModel(*sky_model, player_pos, 1.0f, WHITE);
  rlEnableBackfaceCulling();
  rlEnableDepthTest();
}
}  // namespace raytiles::sky
