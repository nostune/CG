#pragma once
#include <PxPhysicsAPI.h>
#include <DirectXMath.h>

namespace outer_wilds {
namespace components {

/**
 * 表面行走角色控制器
 * 支持在任意形状物体表面行走（重力方向动态变化）
 * 使用 PxController + setUpDirection 实现球面坡度检测
 */
struct CharacterControllerComponent {
    // PhysX Controller（用于角色控制，有内置坡度检测）
    physx::PxController* pxController = nullptr;
    
    // === 移动参数 ===
    float moveSpeed = 6.0f;         // 步行速度 m/s
    float runSpeed = 12.0f;         // 跑步速度 m/s
    float jumpForce = 8.0f;         // 跳跃力
    float airControl = 1.0f ;        // 空中控制系数
    
    // === 胶囊参数 ===
    float capsuleRadius = 0.4f;     // 胶囊半径
    float capsuleHalfHeight = 0.9f; // 胶囊半高（总高1.8m）
    float maxSlopeAngle = 50.0f;    // 最大可行走坡度
    float stepOffset = 0.3f;        // 台阶高度
    
    // === 运行时状态 ===
    bool isGrounded = false;        // 是否站在地面
    bool wasGrounded = false;       // 上一帧是否在地面
    DirectX::XMFLOAT3 velocity = { 0.0f, 0.0f, 0.0f };      // 当前速度（世界空间）
    DirectX::XMFLOAT3 groundNormal = { 0.0f, 1.0f, 0.0f };  // 地面法线
    
    // === 重力对齐 ===
    // localUp = -gravityDir（指向脚底到头顶）
    DirectX::XMFLOAT3 localUp = { 0.0f, 1.0f, 0.0f };       // 当前"上"方向
    DirectX::XMFLOAT3 localForward = { 0.0f, 0.0f, 1.0f };  // 当前"前"方向
    DirectX::XMFLOAT3 localRight = { 1.0f, 0.0f, 0.0f };    // 当前"右"方向
    
    // === 输入状态 ===
    float forwardInput = 0.0f;      // -1 to 1 (S to W)
    float rightInput = 0.0f;        // -1 to 1 (A to D)
    bool wantsToJump = false;       // 是否想跳跃
    bool wantsToRun = false;        // 是否想跑步
    
    // === 相机参数（第一/三人称）===
    float cameraYaw = 0.0f;         // 水平旋转（相对于localForward）
    float cameraPitch = 0.0f;       // 垂直旋转
    DirectX::XMFLOAT3 cameraOffset = { 0.0f, 1.6f, 0.0f };  // 相机相对角色的偏移
};

} // namespace components
} // namespace outer_wilds
