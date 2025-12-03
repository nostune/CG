#pragma once

#include "resources/Mesh.h"
#include "resources/Material.h"
#include "resources/Shader.h"
#include "../scene/components/TransformComponent.h"

namespace outer_wilds
{
    struct RenderItem
    {
        const resources::Mesh* mesh;
        const resources::Material* material;
        const resources::Shader* shader;
        const TransformComponent* transform;
        // Add other properties needed for sorting and rendering, e.g., distance to camera
        float distanceToCamera;
    };
}
