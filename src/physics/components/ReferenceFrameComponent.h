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
    
    /** 上一帧的速度（用于计算速度变化） */
    DirectX::XMFLOAT3 lastVelocity = {0.0f, 0.0f, 0.0f};
    
    /** 当前帧的速度变化（用于参考系补偿） */
    DirectX::XMFLOAT3 deltaVelocity = {0.0f, 0.0f, 0.0f};
    
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
 * 静态吸附模式（staticAttach = true）：
 * - 物体完全不受物理影响
 * - 只更新 Transform，不触碰 PhysX 刚体
 * - 适用于装饰物体、NPC 等不可交互物体
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
    
    /** 
     * 静态吸附模式
     * - true: 物体完全不受物理影响，只更新 Transform
     * - false: 物体受物理影响，通过 PhysX 同步位置（默认）
     */
    bool staticAttach = false;
    
    /**
     * 是否为飞船（特殊处理）
     * - true: 飞船，只在驾驶时启用物理，下船后触地吸附
     * - false: 普通刚体，强制静态吸附，不参与物理模拟
     * 
     * 设计说明：
     * 除飞船外的所有刚体都强制静态吸附，规避 PhysX 不能单独设置参考系的问题
     */
    bool isSpacecraft = false;
    
    /**
     * 触地稳定计时器（仅飞船使用）
     * 用于检测飞船是否满足触地吸附条件
     */
    float groundStableTime = 0.0f;
};

} // namespace components
} // namespace outer_wilds
