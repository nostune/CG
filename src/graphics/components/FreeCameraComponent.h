#pragma once
#include "../../core/ECS.h"
#include <DirectXMath.h>

namespace outer_wilds {
namespace components {

/// <summary>
/// 自由视角相机组件
/// 用于场景浏览、调试等，不受重力和物理约束
/// </summary>
struct FreeCameraComponent : public Component {
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
    
    // 自由相机旋转（弧度）
    float yaw = 0.0f;   // 水平旋转
    float pitch = 0.0f; // 垂直旋转
    
    // 移动速度（基础值，可通过Shift加速）
    float moveSpeed = 15.0f;
    float sprintMultiplier = 3.0f;  // Shift加速倍数
    
    // 视角灵敏度
    float lookSensitivity = 0.002f;
    
    // 是否激活
    bool isActive = false;
};

} // namespace components
} // namespace outer_wilds
