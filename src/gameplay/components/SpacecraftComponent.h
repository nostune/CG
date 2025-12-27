#pragma once
#include <DirectXMath.h>
#include <entt/entt.hpp>

namespace outer_wilds::components {

/**
 * 飞船组件 - 纯物理模拟飞船
 * 
 * 六向控制：
 * - W/S: 前后推进
 * - A/D: 左右平移
 * - Shift/Ctrl: 上下推进
 * - Q/E: 滚转（绕前向轴旋转）
 * 
 * 所有运动通过施加力/力矩实现，完全基于物理模拟
 */
struct SpacecraftComponent {
    // === 飞船状态 ===
    enum class State {
        IDLE,       // 空闲（无人驾驶，kinematic 模式）
        PILOTED     // 驾驶中（dynamic 模式，完全物理模拟）
    };
    
    State currentState = State::IDLE;
    
    // === 驾驶员 ===
    entt::entity pilot = entt::null;
    
    // === 推进力参数（以力为单位，N） ===
    float mainThrust = 15000.0f;       // 主推进力（前后）
    float strafeThrust = 10000.0f;     // 侧向推力（左右）
    float verticalThrust = 12000.0f;   // 垂直推力（上下）
    
    // === 旋转力矩参数（N·m）===
    float rollTorque = 8000.0f;        // 滚转力矩（Q/E）
    float pitchTorque = 6000.0f;       // 俯仰力矩（预留，暂不使用）
    float yawTorque = 6000.0f;         // 偏航力矩（预留，暂不使用）
    
    // === 阻尼参数（PhysX 设置）===
    float linearDamping = 0.3f;        // 线性阻尼
    float angularDamping = 2.0f;       // 角阻尼（更高的值使旋转更容易停止）
    
    // === 质量参数 ===
    float mass = 500.0f;               // 飞船质量（kg）
    
    // === 交互参数 ===
    float interactionDistance = 10.0f; // 上下飞船的交互距离
    DirectX::XMFLOAT3 exitOffset = { 5.0f, 0.0f, 0.0f }; // 下飞船时玩家的偏移位置（局部坐标）
    
    // === 相机参数 ===
    DirectX::XMFLOAT3 cameraOffset = { 0.0f, 2.0f, 0.0f }; // 驾驶位相机偏移（局部坐标）
    
    // === 当前输入状态（每帧更新）===
    float inputForward = 0.0f;    // W/S: -1 到 1
    float inputStrafe = 0.0f;     // A/D: -1 到 1
    float inputVertical = 0.0f;   // Shift/Ctrl: -1 到 1
    float inputRoll = 0.0f;       // Q/E: -1 到 1
    float inputPitch = 0.0f;      // 俱仰: -1 到 1 (上/下箭头或鼠标)
    float inputYaw = 0.0f;        // 偏航: -1 到 1 (左/右箭头或鼠标)
    
    // === 运动状态（物理模拟）===
    DirectX::XMFLOAT3 velocity = { 0.0f, 0.0f, 0.0f };      // 当前速度向量（局部坐标系）
    DirectX::XMFLOAT3 angularVelocity = { 0.0f, 0.0f, 0.0f }; // 当前角速度（局部坐标系）
    
    // === 运动参数 ===
    float maxSpeed = 100.0f;         // 最大速度 (m/s)
    float acceleration = 30.0f;      // 加速度 (m/s²)
    float deceleration = 15.0f;      // 无输入时的减速度 (m/s²)，模拟太空阻力
    float rotationAccel = 3.0f;      // 旋转加速度 (rad/s²)
    float rotationDecel = 2.0f;      // 旋转减速度 (rad/s²)
    float maxRotationSpeed = 2.0f;   // 最大旋转速度 (rad/s)
    
    // === 调试/状态信息 ===
    DirectX::XMFLOAT3 currentVelocity = { 0.0f, 0.0f, 0.0f };
    float currentSpeed = 0.0f;
    DirectX::XMFLOAT3 currentAngularVelocity = { 0.0f, 0.0f, 0.0f };
    
    // === 接地检测 ===
    bool isGrounded = false;
    float groundCheckDistance = 3.0f;  // 接地检测距离
};

/**
 * 玩家飞船交互组件 - 添加到玩家实体上
 */
struct PlayerSpacecraftInteractionComponent {
    entt::entity currentSpacecraft = entt::null;  // 当前驾驶的飞船
    bool isPiloting = false;                       // 是否正在驾驶
    entt::entity nearestSpacecraft = entt::null;  // 最近的可交互飞船
    float distanceToNearest = FLT_MAX;            // 到最近飞船的距离
    bool showInteractionPrompt = false;           // 是否显示交互提示
};

} // namespace outer_wilds::components
