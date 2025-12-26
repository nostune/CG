#pragma once
#include "../../core/ECS.h"
#include <DirectXMath.h>
#include <string>

namespace outer_wilds {
namespace components {

/**
 * SectorComponent - 定义一个 Sector（局部坐标系）
 * 
 * 每个 Sector 是一个独立的物理模拟空间，通常以天体为中心。
 * Sector 内的物体使用局部坐标，不受 Sector 整体运动的影响。
 * 
 * 核心思想：
 * - PhysX 物理模拟在 Sector 局部坐标系中进行
 * - Sector 中心在 PhysX 中始终是 (0,0,0)
 * - 渲染时将局部坐标转换为世界坐标
 */
struct SectorComponent {
    /** Sector 名称（用于调试） */
    std::string name = "Unnamed";
    
    /** 该 Sector 中心的世界坐标（随轨道运动更新） */
    DirectX::XMFLOAT3 worldPosition = {0.0f, 0.0f, 0.0f};
    
    /** 该 Sector 的世界旋转（可选，用于自转） */
    DirectX::XMFLOAT4 worldRotation = {0.0f, 0.0f, 0.0f, 1.0f};
    
    /** 影响半径 - 超出此范围的实体应考虑切换 Sector */
    float influenceRadius = 1000.0f;
    
    /** 父 Sector（用于嵌套，如月球 Sector 的父级是地球 Sector） */
    Entity parentSector = entt::null;
    
    /** 是否是当前活跃的 Sector（玩家所在的 Sector） */
    bool isActive = false;
    
    /** Sector 优先级（用于重叠区域的选择，数值越大优先级越高） */
    int priority = 0;
    
    /** PhysX 物理体是否已初始化到局部坐标原点 */
    bool physicsInitialized = false;
};

/**
 * InSectorComponent - 标记实体属于哪个 Sector
 * 
 * 拥有此组件的实体会：
 * 1. 在 PhysX 中使用局部坐标进行物理模拟
 * 2. 渲染时自动转换为世界坐标
 */
struct InSectorComponent {
    /** 当前所在的 Sector 实体 */
    Entity currentSector = entt::null;
    
    /** 
     * 在 Sector 中的局部位置
     * 这是 PhysX 中的实际位置，也是相对于 Sector 中心的偏移
     */
    DirectX::XMFLOAT3 localPosition = {0.0f, 0.0f, 0.0f};
    
    /** 在 Sector 中的局部旋转 */
    DirectX::XMFLOAT4 localRotation = {0.0f, 0.0f, 0.0f, 1.0f};
    
    /** 在 Sector 中的局部速度（用于 Sector 切换时的速度变换） */
    DirectX::XMFLOAT3 localVelocity = {0.0f, 0.0f, 0.0f};
    
    /** 是否已初始化（首次进入 Sector 时设置局部坐标） */
    bool isInitialized = false;
    
    /** 上一帧的 Sector（用于检测切换） */
    Entity previousSector = entt::null;
};

} // namespace components
} // namespace outer_wilds
