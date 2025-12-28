/**
 * PhysicsSystem.cpp
 * 
 * 物理系统 - 负责 Transform 与 PhysX 之间的同步
 * 
 * 【职责】
 * - SyncTransformsToPhysics: 在 simulate 之前，将 Kinematic 物体的 Transform 同步到 PhysX
 * - SyncPhysicsToTransforms: 在 fetchResults 之后，将 Dynamic 物体的 PhysX 位置同步到 Transform
 * 
 * 【不负责】
 * - PhysX simulate/fetchResults（由 PhysXManager 处理）
 * - 重力计算（由 SectorPhysicsSystem 处理）
 * - 坐标系转换（由 SectorPhysicsSystem 处理）
 */

#include "PhysicsSystem.h"
#include "PhysXManager.h"
#include "../physics/components/RigidBodyComponent.h"
#include "../scene/components/TransformComponent.h"

namespace outer_wilds {

void PhysicsSystem::Initialize() {
    // 禁用全局重力，由 SectorPhysicsSystem 管理
    SetGravity({ 0.0f, 0.0f, 0.0f });
}

void PhysicsSystem::Initialize(std::shared_ptr<Scene> scene) {
    m_Scene = scene;
    Initialize();
}

void PhysicsSystem::Update(float deltaTime, entt::registry& registry) {
    // 在 PhysX simulate 之前同步 Kinematic 物体
    SyncTransformsToPhysics(registry);
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
    // 将 PhysX actor 位置同步到 TransformComponent
    auto view = registry.view<RigidBodyComponent, TransformComponent>();
    for (auto entity : view) {
        auto& rigidBody = view.get<RigidBodyComponent>(entity);
        auto& transform = view.get<TransformComponent>(entity);
        
        if (rigidBody.physxActor && !rigidBody.isKinematic) {
            physx::PxTransform pxTransform = rigidBody.physxActor->getGlobalPose();
            transform.position = DirectX::XMFLOAT3(pxTransform.p.x, pxTransform.p.y, pxTransform.p.z);
            transform.rotation = DirectX::XMFLOAT4(pxTransform.q.x, pxTransform.q.y, pxTransform.q.z, pxTransform.q.w);
        }
    }
}

void PhysicsSystem::SyncTransformsToPhysics(entt::registry& registry) {
    // 将 Kinematic 物体的 TransformComponent 同步到 PhysX
    auto view = registry.view<RigidBodyComponent, TransformComponent>();
    for (auto entity : view) {
        auto& rigidBody = view.get<RigidBodyComponent>(entity);
        auto& transform = view.get<TransformComponent>(entity);
        
        if (!rigidBody.physxActor || !rigidBody.physxActor->getScene()) {
            continue;
        }
        
        if (rigidBody.isKinematic) {
            physx::PxTransform pxTransform(
                physx::PxVec3(transform.position.x, transform.position.y, transform.position.z),
                physx::PxQuat(transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w)
            );
            rigidBody.physxActor->setGlobalPose(pxTransform);
        }
    }
}

} // namespace outer_wilds
