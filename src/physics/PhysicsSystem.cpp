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
    m_TimeAccumulator += deltaTime;
    
    // Fixed timestep physics update
    while (m_TimeAccumulator >= m_FixedTimeStep) {
        // Sync transforms to physics
        SyncTransformsToPhysics(registry);
        
        // Step physics simulation
        auto* scene = PhysXManager::GetInstance().GetScene();
        if (scene) {
            scene->simulate(m_FixedTimeStep);
            scene->fetchResults(true);
        }
        
        // Sync physics back to transforms
        SyncPhysicsToTransforms(registry);
        
        m_TimeAccumulator -= m_FixedTimeStep;
    }
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
