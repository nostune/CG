#pragma once
#include "../resources/Mesh.h"
#include "../resources/Material.h"
#include <memory>

namespace outer_wilds {
namespace components {

/**
 * Component for mesh rendering with material support
 * Follows ECS pattern - each entity with this component represents a renderable mesh object
 */
struct MeshComponent {
    std::shared_ptr<resources::Mesh> mesh;
    std::shared_ptr<resources::Material> material;
    
    // Runtime state
    bool isVisible = true;
    bool castsShadows = true;
    bool receiveShadows = true;
    
    MeshComponent() = default;
    MeshComponent(std::shared_ptr<resources::Mesh> m, std::shared_ptr<resources::Material> mat)
        : mesh(m), material(mat) {}
};

} // namespace components
} // namespace outer_wilds
