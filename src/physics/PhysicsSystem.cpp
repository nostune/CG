#include "PhysicsSystem.h"
#include "PhysXManager.h"
#include "../physics/components/RigidBodyComponent.h"
#include "../physics/components/ColliderComponent.h"
#include "../physics/components/SectorComponent.h"
#include "../scene/components/TransformComponent.h"

namespace outer_wilds {

void PhysicsSystem::Initialize() {
    // PhysX is already initialized in Engine::Initialize()
    
    // Set initial gravity
    SetGravity(m_Gravity);
}

void PhysicsSystem::Initialize(std::shared_ptr<Scene> scene) {
    m_Scene = scene;
    Initialize();
}

void PhysicsSystem::Update(float deltaTime, entt::registry& registry) {
    // 注意：PhysX simulate/fetchResults 由 Engine::Update 中的 PhysXManager::Update 统一调用
    // 这里只做变换同步，不做物理模拟
    
    // Sync transforms to physics (在 PhysX simulate 之前)
    SyncTransformsToPhysics(registry);
    
    // 物理模拟后的同步由 Engine::Update 调用 SectorSystem::PostPhysicsUpdate 完成
    // 对于不在 Sector 中的实体，这里同步
    // SyncPhysicsToTransforms(registry);  // 已移至 PostPhysicsUpdate
}

void PhysicsSystem::Shutdown() {
    // PhysX shutdown is handled in Engine::Shutdown()
}

void PhysicsSystem::SetGravity(const DirectX::XMFLOAT3& gravity) {
    m_Gravity = gravity;
    auto* scene = PhysXManager::GetInstance().GetScene();
    if (scene) {
        scene->setGravity(physx::PxVec3(gravity.x, gravity.y, gravity.z));
    }
}

void PhysicsSystem::SyncPhysicsToTransforms(entt::registry& registry) {
    // Query all entities with RigidBodyComponent and TransformComponent
    auto view = registry.view<RigidBodyComponent, TransformComponent>();
    for (auto entity : view) {
        auto& rigidBody = view.get<RigidBodyComponent>(entity);
        auto& transform = view.get<TransformComponent>(entity);
        
        // 跳过 Sector 实体 - 它们的物理位置由 SectorSystem 特殊管理
        // Sector 的 TransformComponent 保持世界坐标，而物理体在原点
        if (registry.all_of<components::SectorComponent>(entity)) {
            continue;
        }
        
        // 跳过在 Sector 中的实体 - 它们的 TransformComponent 由 SectorSystem.PostPhysicsUpdate 管理
        if (registry.all_of<components::InSectorComponent>(entity)) {
            continue;
        }
        
        if (rigidBody.physxActor) {
            // Get transform from PhysX actor
            physx::PxTransform pxTransform = rigidBody.physxActor->getGlobalPose();
            
            // Update our transform component
            transform.position = DirectX::XMFLOAT3(pxTransform.p.x, pxTransform.p.y, pxTransform.p.z);
            transform.rotation = DirectX::XMFLOAT4(pxTransform.q.x, pxTransform.q.y, pxTransform.q.z, pxTransform.q.w);
        }
    }
}

void PhysicsSystem::SyncTransformsToPhysics(entt::registry& registry) {
    // Query all entities with RigidBodyComponent and TransformComponent
    auto view = registry.view<RigidBodyComponent, TransformComponent>();
    for (auto entity : view) {
        auto& rigidBody = view.get<RigidBodyComponent>(entity);
        auto& transform = view.get<TransformComponent>(entity);
        
        // 跳过 Sector 实体 - 它们的物理位置由 SectorSystem 特殊管理
        // Sector 的物理体需要保持在原点 (0,0,0)，不能同步到 TransformComponent
        if (registry.all_of<components::SectorComponent>(entity)) {
            continue;
        }
        
        // 跳过在 Sector 中的实体 - 它们的 PhysX 位置应该使用局部坐标
        // 而不是 TransformComponent 中的世界坐标
        if (registry.all_of<components::InSectorComponent>(entity)) {
            // 对于在 Sector 中的 kinematic 实体，使用局部坐标更新 PhysX
            if (rigidBody.physxActor && rigidBody.isKinematic) {
                auto& inSector = registry.get<components::InSectorComponent>(entity);
                physx::PxTransform pxTransform(
                    physx::PxVec3(inSector.localPosition.x, inSector.localPosition.y, inSector.localPosition.z),
                    physx::PxQuat(inSector.localRotation.x, inSector.localRotation.y, 
                                  inSector.localRotation.z, inSector.localRotation.w)
                );
                rigidBody.physxActor->setGlobalPose(pxTransform);
            }
            continue;
        }
        
        if (rigidBody.physxActor && rigidBody.isKinematic) {
            // Update PhysX actor from our transform
            physx::PxTransform pxTransform(
                physx::PxVec3(transform.position.x, transform.position.y, transform.position.z),
                physx::PxQuat(transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w)
            );
            rigidBody.physxActor->setGlobalPose(pxTransform);
        }
    }
}

} // namespace outer_wilds
