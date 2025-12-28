/**
 * SectorPhysicsSystem.cpp
 * 
 * 扇区物理系统实现
 */

#include "SectorPhysicsSystem.h"
#include "PhysXManager.h"
#include "components/SectorComponent.h"
#include "components/RigidBodyComponent.h"
#include "components/GravitySourceComponent.h"
#include "components/GravityAffectedComponent.h"
#include "../scene/components/TransformComponent.h"
#include "../gameplay/components/SpacecraftComponent.h"
#include "../core/DebugManager.h"
#include <cmath>

namespace outer_wilds {

using namespace components;

void SectorPhysicsSystem::Initialize() {
    DebugManager::GetInstance().Log("SectorPhysicsSystem", "Initialized");
}

void SectorPhysicsSystem::Initialize(std::shared_ptr<Scene> scene) {
    m_Scene = scene;
    Initialize();
}

void SectorPhysicsSystem::Update(float deltaTime, entt::registry& registry) {
    // 注意：这个 Update 在 Engine 的系统循环中调用
    // 实际的 PrePhysicsUpdate 和 PostPhysicsUpdate 由 Engine 在正确时机调用
}

void SectorPhysicsSystem::Shutdown() {
    DebugManager::GetInstance().Log("SectorPhysicsSystem", "Shutdown");
}

void SectorPhysicsSystem::PrePhysicsUpdate(float deltaTime, entt::registry& registry) {
    // 1. 计算重力方向和强度
    CalculateGravity(registry);
    
    // 2. 应用重力到 PhysX actor
    ApplyGravityForces(registry);
    
    // 3. 同步 Kinematic 物体
    SyncKinematicToPhysX(registry);
}

void SectorPhysicsSystem::PostPhysicsUpdate(float deltaTime, entt::registry& registry) {
    // 从 PhysX 读取 Dynamic 物体位置
    SyncDynamicFromPhysX(registry);
    
    // 飞船稳定化：当飞船接近地面且速度低时，让它sleep
    StabilizeSpacecraft(registry);
    
    // 调试输出：每秒打印一次物理状态
    static float debugTimer = 0.0f;
    debugTimer += deltaTime;
    if (debugTimer >= 1.0f) {
        debugTimer = 0.0f;
        PrintPhysicsDebugInfo(registry);
    }
}

void SectorPhysicsSystem::CalculateGravity(entt::registry& registry) {
    // 获取所有重力源
    auto gravitySourceView = registry.view<GravitySourceComponent, TransformComponent>();
    
    // 获取所有受重力影响的实体
    auto affectedView = registry.view<GravityAffectedComponent, TransformComponent>();
    
    for (auto entity : affectedView) {
        auto& affected = affectedView.get<GravityAffectedComponent>(entity);
        auto& transform = affectedView.get<TransformComponent>(entity);
        
        if (!affected.affectedByGravity) continue;
        
        // 获取实体位置（优先使用 InSectorComponent 的局部坐标）
        DirectX::XMFLOAT3 entityPos = transform.position;
        auto* inSector = registry.try_get<InSectorComponent>(entity);
        if (inSector && inSector->sector != entt::null) {
            // 在扇区内：使用局部坐标计算到扇区中心（原点）的距离
            entityPos = inSector->localPosition;
            
            // 重力指向原点
            float dist = std::sqrt(entityPos.x * entityPos.x + 
                                   entityPos.y * entityPos.y + 
                                   entityPos.z * entityPos.z);
            
            if (dist > 0.01f) {
                affected.currentGravityDir = {
                    -entityPos.x / dist,
                    -entityPos.y / dist,
                    -entityPos.z / dist
                };
            }
            
            // 获取扇区对应的重力源
            auto* sectorGravity = registry.try_get<GravitySourceComponent>(inSector->sector);
            if (sectorGravity && sectorGravity->isActive) {
                affected.currentGravityStrength = sectorGravity->CalculateGravityStrength(dist);
                affected.currentGravitySource = inSector->sector;
            }
        } else {
            // 不在扇区内：查找最近的重力源
            float nearestDist = FLT_MAX;
            entt::entity nearestSource = entt::null;
            
            for (auto sourceEntity : gravitySourceView) {
                auto& source = gravitySourceView.get<GravitySourceComponent>(sourceEntity);
                auto& sourceTransform = gravitySourceView.get<TransformComponent>(sourceEntity);
                
                if (!source.isActive) continue;
                
                float dx = entityPos.x - sourceTransform.position.x;
                float dy = entityPos.y - sourceTransform.position.y;
                float dz = entityPos.z - sourceTransform.position.z;
                float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                
                if (dist < nearestDist && dist < source.GetInfluenceRadius()) {
                    nearestDist = dist;
                    nearestSource = sourceEntity;
                    
                    if (dist > 0.01f) {
                        affected.currentGravityDir = { -dx/dist, -dy/dist, -dz/dist };
                    }
                    affected.currentGravityStrength = source.CalculateGravityStrength(dist);
                }
            }
            affected.currentGravitySource = nearestSource;
        }
    }
}

void SectorPhysicsSystem::ApplyGravityForces(entt::registry& registry) {
    auto view = registry.view<GravityAffectedComponent, RigidBodyComponent>();
    
    for (auto entity : view) {
        auto& affected = view.get<GravityAffectedComponent>(entity);
        auto& rigidBody = view.get<RigidBodyComponent>(entity);
        
        if (!affected.affectedByGravity) continue;
        if (!rigidBody.physxActor) continue;
        if (rigidBody.isKinematic) continue;
        
        auto* dynamicActor = rigidBody.physxActor->is<physx::PxRigidDynamic>();
        if (!dynamicActor) continue;
        
        // 计算重力力 = 质量 * 重力加速度 * 方向
        float mass = rigidBody.mass;
        float strength = affected.currentGravityStrength * affected.gravityScale;
        
        // 验证数值有效性
        if (!std::isfinite(strength) || strength == 0.0f) continue;
        
        float dirMagnitude = std::sqrt(
            affected.currentGravityDir.x * affected.currentGravityDir.x +
            affected.currentGravityDir.y * affected.currentGravityDir.y +
            affected.currentGravityDir.z * affected.currentGravityDir.z
        );
        if (dirMagnitude < 0.001f) continue;  // 方向无效
        
        physx::PxVec3 gravityForce(
            affected.currentGravityDir.x * mass * strength,
            affected.currentGravityDir.y * mass * strength,
            affected.currentGravityDir.z * mass * strength
        );
        
        // 再次验证力的有效性
        if (!gravityForce.isFinite()) continue;
        
        // [来源: SectorPhysicsSystem] 应用重力
        dynamicActor->addForce(gravityForce, physx::PxForceMode::eFORCE);
    }
}

void SectorPhysicsSystem::SyncKinematicToPhysX(entt::registry& registry) {
    auto view = registry.view<RigidBodyComponent, InSectorComponent>();
    
    for (auto entity : view) {
        auto& rigidBody = view.get<RigidBodyComponent>(entity);
        auto& inSector = view.get<InSectorComponent>(entity);
        
        if (!rigidBody.isKinematic) continue;
        if (!rigidBody.physxActor) continue;
        if (!inSector.needsSync) continue;
        
        // [来源: SectorPhysicsSystem] 同步 Kinematic 到 PhysX
        physx::PxTransform pose(
            physx::PxVec3(inSector.localPosition.x, inSector.localPosition.y, inSector.localPosition.z),
            physx::PxQuat(inSector.localRotation.x, inSector.localRotation.y, 
                          inSector.localRotation.z, inSector.localRotation.w)
        );
        rigidBody.physxActor->setGlobalPose(pose);
        inSector.needsSync = false;
    }
}

void SectorPhysicsSystem::SyncDynamicFromPhysX(entt::registry& registry) {
    auto view = registry.view<RigidBodyComponent, InSectorComponent, TransformComponent>();
    
    for (auto entity : view) {
        auto& rigidBody = view.get<RigidBodyComponent>(entity);
        auto& inSector = view.get<InSectorComponent>(entity);
        auto& transform = view.get<TransformComponent>(entity);
        
        if (rigidBody.isKinematic) continue;
        if (!rigidBody.physxActor) continue;
        
        // 读取 PhysX 位置（局部坐标）
        physx::PxTransform pose = rigidBody.physxActor->getGlobalPose();
        inSector.localPosition = { pose.p.x, pose.p.y, pose.p.z };
        inSector.localRotation = { pose.q.x, pose.q.y, pose.q.z, pose.q.w };
        
        // 转换到世界坐标
        if (inSector.sector != entt::null) {
            auto* sector = registry.try_get<SectorComponent>(inSector.sector);
            if (sector) {
                transform.position = LocalToWorld(inSector.localPosition, 
                                                   sector->worldPosition, 
                                                   sector->worldRotation);
                // 组合旋转：实体局部旋转 + 扇区世界旋转
                transform.rotation = CombineRotations(inSector.localRotation, sector->worldRotation);
            }
        }
    }
    
    // 同步所有扇区内实体的世界坐标
    SyncSectorEntities(registry);
}

void SectorPhysicsSystem::SyncSectorEntities(entt::registry& registry) {
    // 对于所有在扇区内的实体，更新它们的世界坐标
    // 这确保当扇区（星球）移动时，扇区内的所有实体跟随移动
    auto view = registry.view<InSectorComponent, TransformComponent>();
    
    for (auto entity : view) {
        auto& inSector = view.get<InSectorComponent>(entity);
        auto& transform = view.get<TransformComponent>(entity);
        
        if (inSector.sector == entt::null) continue;
        
        auto* sector = registry.try_get<SectorComponent>(inSector.sector);
        if (!sector) continue;
        
        // 使用扇区的世界位置和旋转来计算实体的世界坐标
        transform.position = LocalToWorld(inSector.localPosition, 
                                           sector->worldPosition, 
                                           sector->worldRotation);
        transform.rotation = CombineRotations(inSector.localRotation, sector->worldRotation);
    }
}

void SectorPhysicsSystem::TransferEntityToSector(entt::registry& registry, entt::entity entity, entt::entity newSector) {
    auto* inSector = registry.try_get<InSectorComponent>(entity);
    auto* transform = registry.try_get<TransformComponent>(entity);
    
    if (!inSector) {
        inSector = &registry.emplace<InSectorComponent>(entity);
    }
    
    // 如果有旧扇区，需要做坐标转换
    if (inSector->sector != entt::null && inSector->sector != newSector && transform) {
        // 获取当前世界坐标
        DirectX::XMFLOAT3 worldPos = transform->position;
        
        // 获取新扇区
        auto* newSectorComp = registry.try_get<SectorComponent>(newSector);
        if (newSectorComp) {
            // 转换到新扇区的局部坐标
            inSector->localPosition = WorldToLocal(worldPos, 
                                                    newSectorComp->worldPosition,
                                                    newSectorComp->worldRotation);
        }
    }
    
    inSector->sector = newSector;
    inSector->needsSync = true;
    inSector->isInitialized = true;
    
    DebugManager::GetInstance().Log("SectorPhysicsSystem", "Entity transferred to new sector");
}

DirectX::XMFLOAT3 SectorPhysicsSystem::LocalToWorld(
    const DirectX::XMFLOAT3& localPos,
    const DirectX::XMFLOAT3& sectorWorldPos,
    const DirectX::XMFLOAT4& sectorWorldRot
) {
    // 加载扇区旋转四元数
    DirectX::XMVECTOR rotQuat = DirectX::XMLoadFloat4(&sectorWorldRot);
    
    // 检查是否是单位四元数（无旋转）
    DirectX::XMVECTOR identity = DirectX::XMQuaternionIdentity();
    DirectX::XMVECTOR diff = DirectX::XMVectorSubtract(rotQuat, identity);
    float length = DirectX::XMVectorGetX(DirectX::XMVector4Length(diff));
    
    DirectX::XMFLOAT3 worldPos;
    
    if (length < 0.001f) {
        // 无旋转，直接平移
        worldPos = {
            sectorWorldPos.x + localPos.x,
            sectorWorldPos.y + localPos.y,
            sectorWorldPos.z + localPos.z
        };
    } else {
        // 有旋转：先旋转局部位置，再平移
        DirectX::XMVECTOR localVec = DirectX::XMLoadFloat3(&localPos);
        DirectX::XMVECTOR rotatedVec = DirectX::XMVector3Rotate(localVec, rotQuat);
        DirectX::XMFLOAT3 rotatedPos;
        DirectX::XMStoreFloat3(&rotatedPos, rotatedVec);
        
        worldPos = {
            sectorWorldPos.x + rotatedPos.x,
            sectorWorldPos.y + rotatedPos.y,
            sectorWorldPos.z + rotatedPos.z
        };
    }
    
    return worldPos;
}

DirectX::XMFLOAT3 SectorPhysicsSystem::WorldToLocal(
    const DirectX::XMFLOAT3& worldPos,
    const DirectX::XMFLOAT3& sectorWorldPos,
    const DirectX::XMFLOAT4& sectorWorldRot
) {
    // 先平移（世界坐标 - 扇区世界位置）
    DirectX::XMFLOAT3 offset = {
        worldPos.x - sectorWorldPos.x,
        worldPos.y - sectorWorldPos.y,
        worldPos.z - sectorWorldPos.z
    };
    
    // 加载扇区旋转四元数的逆
    DirectX::XMVECTOR rotQuat = DirectX::XMLoadFloat4(&sectorWorldRot);
    DirectX::XMVECTOR invRotQuat = DirectX::XMQuaternionInverse(rotQuat);
    
    // 检查是否是单位四元数（无旋转）
    DirectX::XMVECTOR identity = DirectX::XMQuaternionIdentity();
    DirectX::XMVECTOR diff = DirectX::XMVectorSubtract(rotQuat, identity);
    float length = DirectX::XMVectorGetX(DirectX::XMVector4Length(diff));
    
    if (length < 0.001f) {
        // 无旋转，直接返回偏移
        return offset;
    }
    
    // 有旋转：用逆旋转偏移
    DirectX::XMVECTOR offsetVec = DirectX::XMLoadFloat3(&offset);
    DirectX::XMVECTOR localVec = DirectX::XMVector3Rotate(offsetVec, invRotQuat);
    DirectX::XMFLOAT3 localPos;
    DirectX::XMStoreFloat3(&localPos, localVec);
    
    return localPos;
}

DirectX::XMFLOAT4 SectorPhysicsSystem::CombineRotations(
    const DirectX::XMFLOAT4& localRot,
    const DirectX::XMFLOAT4& sectorWorldRot
) {
    // 组合旋转：sectorWorldRot * localRot
    DirectX::XMVECTOR localQ = DirectX::XMLoadFloat4(&localRot);
    DirectX::XMVECTOR sectorQ = DirectX::XMLoadFloat4(&sectorWorldRot);
    DirectX::XMVECTOR combinedQ = DirectX::XMQuaternionMultiply(localQ, sectorQ);
    combinedQ = DirectX::XMQuaternionNormalize(combinedQ);
    
    DirectX::XMFLOAT4 result;
    DirectX::XMStoreFloat4(&result, combinedQ);
    return result;
}

void SectorPhysicsSystem::PrintPhysicsDebugInfo(entt::registry& registry) {
    static int frameCount = 0;
    frameCount++;
    
    auto view = registry.view<RigidBodyComponent, InSectorComponent, GravityAffectedComponent>();
    
    for (auto entity : view) {
        auto& rigidBody = view.get<RigidBodyComponent>(entity);
        auto& inSector = view.get<InSectorComponent>(entity);
        auto& gravity = view.get<GravityAffectedComponent>(entity);
        
        if (!rigidBody.physxActor) continue;
        
        auto* dynamicActor = rigidBody.physxActor->is<physx::PxRigidDynamic>();
        if (!dynamicActor) continue;
        
        // 获取 PhysX 状态
        physx::PxTransform pose = dynamicActor->getGlobalPose();
        physx::PxVec3 velocity = dynamicActor->getLinearVelocity();
        bool isSleeping = dynamicActor->isSleeping();
        
        // 计算到原点的距离（星球表面检测）
        float dist = std::sqrt(pose.p.x * pose.p.x + pose.p.y * pose.p.y + pose.p.z * pose.p.z);
        
    }
}

void SectorPhysicsSystem::StabilizeSpacecraft(entt::registry& registry) {
    const float PLANET_RADIUS = 64.0f;  // 星球半径
    const float GROUND_THRESHOLD = 1.0f; // 距离地面阈值
    const float VELOCITY_THRESHOLD = 0.5f;  // 速度阈值
    const float ANGULAR_VELOCITY_THRESHOLD = 0.3f;  // 角速度阈值
    
    auto view = registry.view<SpacecraftComponent, RigidBodyComponent, InSectorComponent>();
    
    for (auto entity : view) {
        auto& spacecraft = view.get<SpacecraftComponent>(entity);
        auto& rigidBody = view.get<RigidBodyComponent>(entity);
        auto& inSector = view.get<InSectorComponent>(entity);
        
        // 只处理IDLE状态的飞船
        if (spacecraft.currentState != SpacecraftComponent::State::IDLE) continue;
        if (!rigidBody.physxActor) continue;
        
        auto* dynamicActor = rigidBody.physxActor->is<physx::PxRigidDynamic>();
        if (!dynamicActor) continue;
        
        // 计算到原点的距离
        float dist = std::sqrt(
            inSector.localPosition.x * inSector.localPosition.x +
            inSector.localPosition.y * inSector.localPosition.y +
            inSector.localPosition.z * inSector.localPosition.z
        );
        
        // 计算到地面的距离
        float groundDist = dist - PLANET_RADIUS;
        
        // 获取当前速度
        physx::PxVec3 linearVel = dynamicActor->getLinearVelocity();
        physx::PxVec3 angularVel = dynamicActor->getAngularVelocity();
        float speed = linearVel.magnitude();
        float angularSpeed = angularVel.magnitude();
        
        // 如果飞船接近地面且速度低，让它稳定下来
        if (groundDist < GROUND_THRESHOLD) {
            // 强制降低角速度
            if (angularSpeed > ANGULAR_VELOCITY_THRESHOLD) {
                dynamicActor->setAngularVelocity(angularVel * 0.8f);  // 快速衰减
            }
            
            // 如果速度足够低，让飞船sleep
            if (speed < VELOCITY_THRESHOLD && angularSpeed < ANGULAR_VELOCITY_THRESHOLD) {
                // 清零速度
                dynamicActor->setLinearVelocity(physx::PxVec3(0.0f));
                dynamicActor->setAngularVelocity(physx::PxVec3(0.0f));
                
                // 让飞船进入sleep状态
                if (!dynamicActor->isSleeping()) {
                    dynamicActor->putToSleep();
                    spacecraft.isGrounded = true;
                }
            }
        } else {
            spacecraft.isGrounded = false;
        }
    }
}

} // namespace outer_wilds
