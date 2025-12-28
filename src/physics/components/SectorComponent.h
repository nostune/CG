/**
 * SectorComponent.h
 * 
 * 扇区组件定义
 * - SectorComponent: 扇区定义（附加到星球）
 * - InSectorComponent: 扇区内实体（附加到玩家/物体）
 */

#pragma once
#include <DirectXMath.h>
#include <entt/entt.hpp>
#include <PxPhysicsAPI.h>
#include <string>

namespace outer_wilds {
namespace components {

/**
 * 扇区定义组件 - 附加到星球上
 * 
 * 扇区 = 一个局部物理坐标系
 * - PhysX 物理在扇区原点运行
 * - 渲染使用世界坐标 = 扇区世界位置 + 局部坐标
 */
struct SectorComponent {
    // 基本信息
    std::string name = "Unnamed";           // 扇区名称（调试用）
    float influenceRadius = 100.0f;         // 影响半径（米）
    int priority = 0;                       // 优先级（多扇区重叠时）
    bool isActive = true;                   // 是否激活
    
    // 世界坐标（扇区在世界中的位置）
    DirectX::XMFLOAT3 worldPosition = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 worldRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    
    // PhysX 地面碰撞体（在扇区原点）
    physx::PxRigidStatic* physxGround = nullptr;
};

/**
 * 扇区内实体组件 - 附加到玩家/物体上
 * 
 * 标记实体属于哪个扇区，并存储局部坐标
 */
struct InSectorComponent {
    // 所属扇区
    entt::entity sector = entt::null;
    
    // 扇区局部坐标（PhysX 使用这个）
    DirectX::XMFLOAT3 localPosition = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 localRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    
    // 状态
    bool isInitialized = false;             // 是否已初始化
    bool needsSync = true;                  // 是否需要同步到 PhysX
};

} // namespace components
} // namespace outer_wilds
