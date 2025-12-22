#pragma once
#include <DirectXMath.h>
#include <entt/entt.hpp>

namespace outer_wilds::components {

/**
 * 飞船组件 - 存储飞船状态和控制参数
 */
struct SpacecraftComponent {
    // === 飞船状态 ===
    enum class State {
        IDLE,           // 空闲（无人驾驶）
        PILOTED,        // 有人驾驶（自由飞行模式）
        LANDING_ASSIST  // 辅助起降模式（自动对齐星球表面）
    };
    
    State currentState = State::IDLE;
    
    // === 当前驾驶员 ===
    entt::entity pilot = entt::null;
    
    // === 推进力参数 ===
    float thrustPower = 80.0f;          // 水平推进力（N/kg，即加速度）
    float verticalThrustPower = 100.0f; // 垂直推进力（用于摆脱重力，需要大于重力加速度）
    
    // === 旋转控制参数（角加速度模型） ===
    float angularAcceleration = 8.0f;   // 角加速度（rad/s²）- 提高响应速度
    float maxAngularSpeed = 4.0f;       // 最大角速度（rad/s）- 提高转向速度
    float angularDamping = 3.0f;        // 角速度阻尼（无输入时减速）
    float mouseSensitivity = 0.25f;     // 鼠标灵敏度 - 提高
    
    // === 当前角速度状态 ===
    float currentYawSpeed = 0.0f;       // 当前偏航角速度
    float currentPitchSpeed = 0.0f;     // 当前俯仰角速度
    
    // === 交互参数 ===
    float interactionDistance = 5.0f;   // 上下飞船的交互距离
    DirectX::XMFLOAT3 exitOffset = { 3.0f, 0.0f, 0.0f }; // 下飞船时玩家的偏移位置
    
    // === 辅助起降参数 ===
    float landingAssistAltitude = 100.0f;   // 触发辅助起降的高度阈值
    float landingAlignSpeed = 3.0f;          // 对齐速度
    bool landingAssistAvailable = false;     // 是否可用辅助起降
    
    // === 相机参数 ===
    DirectX::XMFLOAT3 cameraOffset = { 0.0f, 1.5f, 0.0f }; // 驾驶位相机偏移
    
    // === 输入状态（每帧更新）===
    float inputForward = 0.0f;   // W/S
    float inputStrafe = 0.0f;    // A/D
    float inputVertical = 0.0f;  // Shift/Ctrl
    float inputYaw = 0.0f;       // 鼠标X（目标角速度方向）
    float inputPitch = 0.0f;     // 鼠标Y（目标角速度方向）
    
    // === 当前旋转（欧拉角）===
    float yaw = 0.0f;
    float pitch = 0.0f;
    
    // === 辅助信息 ===
    entt::entity nearestCelestial = entt::null;  // 最近的天体
    float distanceToSurface = 0.0f;               // 到最近天体表面的距离
};

/**
 * 可驾驶标记组件 - 标记玩家可以驾驶飞船
 */
struct PilotableComponent {
    bool canPilot = true;
};

/**
 * 玩家飞船交互组件 - 添加到玩家实体上
 */
struct PlayerSpacecraftInteractionComponent {
    entt::entity currentSpacecraft = entt::null;  // 当前驾驶的飞船
    bool isPiloting = false;                       // 是否正在驾驶
    entt::entity nearestSpacecraft = entt::null;  // 最近的可交互飞船
    float distanceToNearest = FLT_MAX;            // 到最近飞船的距离
};

} // namespace outer_wilds::components
