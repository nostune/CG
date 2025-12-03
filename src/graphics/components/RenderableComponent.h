#pragma once

#include "../resources/Mesh.h"
#include "../resources/Shader.h"
#include "../resources/Material.h"
#include <memory>

namespace outer_wilds {
namespace components {

struct RenderableComponent {
    std::shared_ptr<resources::Mesh> mesh;
    std::shared_ptr<resources::Shader> shader;
    std::shared_ptr<resources::Material> material;
};

} // namespace components
} // namespace outer_wilds
