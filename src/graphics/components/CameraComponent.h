#pragma once
#include "../../core/ECS.h"
#include <DirectXMath.h>

namespace outer_wilds {
namespace components {

struct CameraComponent : public Component {
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 target = {0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT3 up = {0.0f, 1.0f, 0.0f};
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4X4 projection;
    
    float fov = 75.0f;
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    
    bool isActive = true;
    
    // First-person camera control
    float yaw = 0.0f;   // Horizontal rotation (radians)
    float pitch = 0.0f; // Vertical rotation (radians)
    float moveSpeed = 10.0f;
    float lookSensitivity = 0.002f;
};

} // namespace components
} // namespace outer_wilds
