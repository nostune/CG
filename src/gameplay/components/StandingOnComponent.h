#pragma once
#include "../../core/ECS.h"
#include <DirectXMath.h>

namespace outer_wilds {
namespace components {

/**
 * 站立组件 - 记录实体"站在"哪个天体上
 * 
 * 用于解决动态星球（如轨道运动的月球）上的玩家跟随问题
 */
struct StandingOnComponent {
    /** 当前站立的天体实体（通常是最近的重力源） */
    Entity standingOnEntity = entt::null;
    
    /** 上一帧站立的天体（用于检测切换） */
    Entity previousStandingEntity = entt::null;
    
    /** 相对于天体中心的位置（天体局部坐标） */
    DirectX::XMFLOAT3 relativePosition = {0.0f, 0.0f, 0.0f};
    
    /** 是否启用跟随（站在移动天体上时自动跟随） */
    bool followEnabled = true;
    
    /** 检测距离阈值（与星球表面距离小于此值时认为"站在"上面） */
    float detectionThreshold = 2.0f;
};

} // namespace components
} // namespace outer_wilds
