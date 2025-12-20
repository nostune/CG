#pragma once
#include "../../core/ECS.h"
#include <DirectXMath.h>

namespace outer_wilds {
namespace components {

/**
 * 参考系组件 - 定义一个可以作为参考系的天体
 * 
 * 核心设计（修正版）：
 * - 天体可能有非物理的轨道运动（由OrbitSystem驱动）
 * - 需要记录每帧的位移增量，以便附着物体能跟随运动
 * - 【重要】不禁用物理！只是补偿天体运动带来的位移
 * 
 * 用途：行星、月球等具有预设轨道运动的天体
 */
struct ReferenceFrameComponent {
    /** 此参考系的名称（调试用） */
    const char* name = "Unknown";
    
    /** 参考系是否激活 */
    bool isActive = true;
    
    /** 上一帧的世界坐标位置（用于计算位移增量） */
    DirectX::XMFLOAT3 lastPosition = {0.0f, 0.0f, 0.0f};
    
    /** 当前帧的位移增量（当前位置 - 上一帧位置） */
    DirectX::XMFLOAT3 deltaPosition = {0.0f, 0.0f, 0.0f};
    
    /** 计算得出的线性速度（米/秒），用于调试 */
    DirectX::XMFLOAT3 velocity = {0.0f, 0.0f, 0.0f};
    
    /** 是否已初始化（第一帧需要记录初始位置） */
    bool isInitialized = false;
};

/**
 * 附着到参考系组件 - 物体附着在某个参考系上
 * 
 * 工作原理（本地坐标方案）：
 * 1. 首次附着时，计算物体相对于天体的本地坐标
 * 2. 每帧：物体全局位置 = 天体位置 + 本地坐标
 * 3. 物理模拟后，更新本地坐标以反映物理运动
 * 
 * 优势：
 * - 完全消除累积误差（不用位移增量）
 * - 物体精确跟随天体圆弧运动
 * - 物理运动正常工作
 */
struct AttachedToReferenceFrameComponent {
    /** 附着的参考系实体（通常是当前重力源） */
    Entity referenceFrame = entt::null;
    
    /** 物体相对于参考系的本地坐标 */
    DirectX::XMFLOAT3 localOffset = {0.0f, 0.0f, 0.0f};
    
    /** 是否已初始化本地坐标 */
    bool isInitialized = false;
    
    /** 是否自动附着到当前重力源 */
    bool autoAttach = true;
    
    /** 是否启用位置同步 */
    bool enableCompensation = true;
};

} // namespace components
} // namespace outer_wilds
