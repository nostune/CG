#pragma once
#include "../../core/ECS.h"
#include <DirectXMath.h>

namespace outer_wilds {

struct TransformComponent : public Component {
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 rotation = { 0.0f, 0.0f, 0.0f, 1.0f }; // Quaternion
    DirectX::XMFLOAT3 scale = { 1.0f, 1.0f, 1.0f };
    
    DirectX::XMMATRIX GetWorldMatrix() const;
};

} // namespace outer_wilds
