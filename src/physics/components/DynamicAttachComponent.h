#pragma once
#include "../../core/ECS.h"
#include <DirectXMath.h>

namespace outer_wilds {
namespace components {

/**
 * 动态吸附组件 - 管理动态刚体的状态切换
 * 
 * 用于需要物理交互但也要跟随星球的物体（如飞船、可推动的箱子）
 * 
 * 状态说明：
 * - DYNAMIC: 正常物理模式，受重力和碰撞影响
 * - GROUNDED: 接地静态模式，跟随星球运动，不受物理影响
 * 
 * 切换条件：
 * - Dynamic → Grounded: 接地 + 速度低于阈值 + 持续一段时间
 * - Grounded → Dynamic: 受到外力 / 速度超过阈值 / 手动唤醒
 */
struct DynamicAttachComponent {
    enum class State {
        DYNAMIC,    // 动态：受物理影响
        GROUNDED    // 接地：静态吸附模式
    };
    
    /** 当前状态 */
    State currentState = State::DYNAMIC;
    
    /** 接地检测射线长度（从物体中心向重力方向） */
    float groundCheckDistance = 1.0f;
    
    /** 速度阈值：低于此值视为静止 */
    float velocityThreshold = 0.3f;
    
    /** 接地稳定时间：接地且静止持续此时间后切换到 GROUNDED */
    float groundedStableTime = 0.5f;
    
    /** 唤醒速度阈值：速度超过此值时从 GROUNDED 切换回 DYNAMIC */
    float wakeUpVelocityThreshold = 1.0f;
    
    /** 当前接地稳定计时器 */
    float currentStableTime = 0.0f;
    
    /** 是否当前帧检测到接地 */
    bool isGrounded = false;
    
    /** 手动唤醒标志（外部设置，下一帧切换到 DYNAMIC） */
    bool wakeUpRequested = false;
    
    /** 存储 GROUNDED 状态下的本地坐标 */
    DirectX::XMFLOAT3 groundedLocalOffset = {0.0f, 0.0f, 0.0f};
    
    /** 存储 GROUNDED 状态下的旋转 */
    DirectX::XMFLOAT4 groundedRotation = {0.0f, 0.0f, 0.0f, 1.0f};
    
    /** GROUNDED 状态是否已初始化本地坐标 */
    bool groundedInitialized = false;
};

} // namespace components
} // namespace outer_wilds
