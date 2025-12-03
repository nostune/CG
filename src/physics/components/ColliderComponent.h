#pragma once
#include "../../core/ECS.h"
#include <DirectXMath.h>

namespace outer_wilds {

enum class ColliderType {
    Box,
    Sphere,
    Capsule,
    Mesh
};

struct ColliderComponent : public Component {
    ColliderType type = ColliderType::Box;
    
    // Box collider
    DirectX::XMFLOAT3 boxExtent = { 0.5f, 0.5f, 0.5f };
    
    // Sphere collider
    float sphereRadius = 0.5f;
    
    // Capsule collider
    float capsuleRadius = 0.5f;
    float capsuleHeight = 1.0f;
    
    // Offset from transform
    DirectX::XMFLOAT3 centerOffset = { 0.0f, 0.0f, 0.0f };
    
    // Physics material properties
    bool isTrigger = false;
};

} // namespace outer_wilds
