#pragma once
#include "../../core/ECS.h"
#include <DirectXMath.h>

namespace outer_wilds {
namespace components {

/**
 * 天体附着组件 - 让物体固定在天体表面上（相对位置不变）
 * 
 * 用途：
 * - 飞船停靠在行星表面
 * - 建筑物固定在行星上
 * - 其他需要跟随行星运动的静态物体
 * 
 * 与StandingOnComponent的区别：
 * - StandingOnComponent用于玩家，会检测脚下的天体
 * - CelestialAttachmentComponent用于静态物体，直接指定父天体
 */
struct CelestialAttachmentComponent {
    /** 附着的天体实体 */
    Entity parentCelestial = entt::null;
    
    /** 相对于天体中心的偏移位置（天体局部坐标） */
    DirectX::XMFLOAT3 relativePosition = {0.0f, 0.0f, 0.0f};
    
    /** 相对于天体的旋转（四元数） */
    DirectX::XMFLOAT4 relativeRotation = {0.0f, 0.0f, 0.0f, 1.0f};
    
    /** 是否已初始化相对位置 */
    bool isInitialized = false;
    
    /** 是否启用跟随 */
    bool followEnabled = true;
};

} // namespace components
} // namespace outer_wilds
