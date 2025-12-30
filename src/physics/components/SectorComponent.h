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
 * 
 * 扇区切换规则：
 * - 实体进入扇区范围(influenceRadius)时，自动切换到该扇区
 * - 多个扇区重叠时，按优先级选择（数值越大优先级越高）
 * - 嵌套扇区（如月球在地球范围内），月球应有更高优先级
 */
struct SectorComponent {
    // 基本信息
    std::string name = "Unnamed";           // 扇区名称（调试用）
    float influenceRadius = 100.0f;         // 影响半径（米）- 用于扇区切换检测
    float planetRadius = 50.0f;             // 星球实际半径（米）- 用于碰撞体
    int priority = 0;                       // 优先级（多扇区重叠时，数值越大越优先）
    bool isActive = true;                   // 是否激活
    
    // 父扇区（用于嵌套扇区，如月球的父扇区是地球）
    entt::entity parentSector = entt::null;
    
    // 世界坐标（扇区在世界中的位置）
    DirectX::XMFLOAT3 worldPosition = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 worldRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    
    // 世界速度（扇区/星球的运动速度，用于扇区切换时的速度补偿）
    DirectX::XMFLOAT3 worldVelocity = { 0.0f, 0.0f, 0.0f };
    
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
    
    // ========================================
    // 扇区过渡相关（用于平滑切换）
    // ========================================
    
    // 速度补偿（扇区切换时需要逐渐应用的速度差）
    DirectX::XMFLOAT3 velocityCompensation = { 0.0f, 0.0f, 0.0f };
    
    // 过渡进度 (1.0 = 刚开始过渡, 0.0 = 过渡完成)
    float transitionProgress = 0.0f;
    
    // 切换冷却时间（切换后多久不能再次切换）
    float switchCooldown = 0.0f;
    
    // 冷却时间常量（秒）
    static constexpr float SWITCH_COOLDOWN_DURATION = 1.0f;
    
    // 过渡时间（秒）
    static constexpr float TRANSITION_DURATION = 0.5f;
};

} // namespace components
} // namespace outer_wilds
