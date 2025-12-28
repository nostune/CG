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
    
    // 飞船稳定化：当飞船接近地面且速度低时让它sleep
    void StabilizeSpacecraft(entt::registry& registry);
    
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

    std::shared_ptr<Scene> m_Scene;
};

} // namespace outer_wilds
