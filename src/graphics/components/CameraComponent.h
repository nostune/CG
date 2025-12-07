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
    float yaw = 0.0f;   // Horizontal rotation (radians) - DEPRECATED for spherical gravity
    float pitch = 0.0f; // Vertical rotation (radians) - DEPRECATED for spherical gravity
    float moveSpeed = 10.0f;
    float lookSensitivity = 0.002f;
    
    // === 球面重力相机系统：局部参考系 ===
    // 核心思想：相机旋转相对于玩家的局部坐标系，而非世界坐标系
    
    // 局部坐标系（每帧根据重力方向更新）
    DirectX::XMFLOAT3 localUp = {0.0f, 1.0f, 0.0f};        // 重力反方向（远离星球中心）
    DirectX::XMFLOAT3 localForward = {0.0f, 0.0f, 1.0f};   // 切平面上的参考前方
    DirectX::XMFLOAT3 localRight = {1.0f, 0.0f, 0.0f};     // 切平面上的参考右方
    
    // 相对旋转（相对于局部坐标系，由鼠标输入修改）
    float relativeYaw = 0.0f;    // 相对localForward的水平旋转角（弧度）
    float relativePitch = 0.0f;  // 相对切平面的俯仰角（弧度）
};

} // namespace components
} // namespace outer_wilds
