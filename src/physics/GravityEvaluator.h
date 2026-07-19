#pragma once

#include "components/GravitySourceComponent.h"
#include <DirectXMath.h>

namespace outer_wilds {

struct GravitySample {
    DirectX::XMFLOAT3 acceleration = {0.0f, 0.0f, 0.0f};
    float strength = 0.0f;
    float distance = 0.0f;
};

class GravityEvaluator {
public:
    static GravitySample EvaluateLocal(
        const components::GravitySourceComponent& source,
        const DirectX::XMFLOAT3& localPosition,
        float gravityScale = 1.0f);
};

} // namespace outer_wilds
