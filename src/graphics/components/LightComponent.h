#pragma once
#include "../../core/ECS.h"
#include <DirectXMath.h>

namespace outer_wilds {

enum class LightType {
    Directional,
    Point,
    Spot
};

struct LightComponent : public Component {
    LightType type = LightType::Directional;
    
    DirectX::XMFLOAT3 color = { 1.0f, 1.0f, 1.0f };
    float intensity = 1.0f;
    
    // Point/Spot light properties
    float range = 10.0f;
    float attenuation = 1.0f;
    
    // Spot light properties
    float innerConeAngle = 30.0f;
    float outerConeAngle = 45.0f;
    
    bool castShadows = true;
};

} // namespace outer_wilds
