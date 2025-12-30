/**
 * SectorPhysicsSystem.h
 * 
 * 扇区物理系统 - 管理扇区内的物理模拟
 * 
 * 职责：
 * - PrePhysicsUpdate: 计算重力、应用力、同步 Kinematic 物体
 * - PostPhysicsUpdate: 读取 PhysX 结果、坐标转换、更新 Transform
 * - 扇区切换管理
 */

#pragma once
#include "../core/ECS.h"
#include "../scene/Scene.h"
#include <DirectXMath.h>
#include <memory>

namespace outer_wilds {

class SectorPhysicsSystem : public System {
public:
    SectorPhysicsSystem() = default;
    ~SectorPhysicsSystem() override = default;

    void Initialize() override;
    void Initialize(std::shared_ptr<Scene> scene);
    void Update(float deltaTime, entt::registry& registry) override;
    void Shutdown() override;
    
    /**
     * 物理模拟前更新
     * - 计算并应用重力
     * - 同步 Kinematic 物体到 PhysX
     */
    void PrePhysicsUpdate(float deltaTime, entt::registry& registry);
    
    /**
     * 物理模拟后更新
     * - 读取 PhysX Dynamic 物体位置
     * - 转换局部坐标到世界坐标
     * - 更新 TransformComponent
     */
    void PostPhysicsUpdate(float deltaTime, entt::registry& registry);
    
    /**
     * 将实体转移到新扇区
     */
    void TransferEntityToSector(entt::registry& registry, entt::entity entity, entt::entity newSector);

private:
    // 计算实体受到的重力
    void CalculateGravity(entt::registry& registry);
    
    // 应用重力到 PhysX actor
    void ApplyGravityForces(entt::registry& registry);
    
    // 同步 Kinematic 物体的 Transform 到 PhysX
    void SyncKinematicToPhysX(entt::registry& registry);
    
    // 从 PhysX 读取 Dynamic 物体位置并更新 Transform
    void SyncDynamicFromPhysX(entt::registry& registry);
    
    // 局部坐标转世界坐标
    DirectX::XMFLOAT3 LocalToWorld(
        const DirectX::XMFLOAT3& localPos,
        const DirectX::XMFLOAT3& sectorWorldPos,
        const DirectX::XMFLOAT4& sectorWorldRot
    );
    
    // 调试输出
    void PrintPhysicsDebugInfo(entt::registry& registry);
    
    // 每30秒打印当前扇区信息
    void PrintCurrentSectorInfo(entt::registry& registry);
    
    // 飞船稳定化：当飞船接近地面且速度低时让它sleep
    void StabilizeSpacecraft(entt::registry& registry);
    
    // 应用扇区切换的渐进式速度补偿
    void ApplyVelocityCompensation(float deltaTime, entt::registry& registry);
    
    // 同步扇区内实体的世界坐标（当扇区/星球移动时调用）
    void SyncSectorEntities(entt::registry& registry);
    
    // 世界坐标转局部坐标
    DirectX::XMFLOAT3 WorldToLocal(
        const DirectX::XMFLOAT3& worldPos,
        const DirectX::XMFLOAT3& sectorWorldPos,
        const DirectX::XMFLOAT4& sectorWorldRot
    );
    
    // 旋转组合（用于扇区旋转）
    DirectX::XMFLOAT4 CombineRotations(
        const DirectX::XMFLOAT4& localRot,
        const DirectX::XMFLOAT4& sectorWorldRot
    );
    
    // 扇区碰撞体管理
    void UpdateSectorCollisions(entt::registry& registry);
    void SetSectorCollisionEnabled(entt::registry& registry, entt::entity sector, bool enabled);
    
    // 检测并执行扇区切换（基于世界坐标距离、优先级和滞后机制）
    void CheckAndSwitchSectors(entt::registry& registry);
    
    // 为单个实体找到最佳扇区（带滞后机制防止边界振荡）
    // currentSector: 当前扇区（用于滞后计算）
    // enterHysteresis: 进入新扇区的阈值系数（如0.92表示需要深入到influenceRadius*0.92才进入）
    // exitHysteresis: 退出当前扇区的阈值系数（如1.08表示需要超出influenceRadius*1.08才退出）
    entt::entity FindBestSectorForEntity(entt::registry& registry, const DirectX::XMFLOAT3& worldPos,
                                          entt::entity currentSector = entt::null, 
                                          float enterHysteresis = 1.0f, float exitHysteresis = 1.0f);
    
    // 将实体的 PhysX Actor 转移到新扇区
    void TransferPhysXActorToSector(entt::registry& registry, entt::entity entity, 
                                     entt::entity oldSector, entt::entity newSector);

    std::shared_ptr<Scene> m_Scene;
    
    // 当前激活的碰撞体扇区（用于追踪切换）
    entt::entity m_ActiveCollisionSector = entt::null;
};

} // namespace outer_wilds
