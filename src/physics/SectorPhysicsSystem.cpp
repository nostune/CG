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
#include "../gameplay/components/CharacterControllerComponent.h"
#include "../core/DebugManager.h"
#include <algorithm>
#include <cmath>
#include <sstream>

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
    // 0. 检测并执行扇区切换（必须在碰撞体切换之前！）
    CheckAndSwitchSectors(deltaTime, registry);
    
    // 1. 扇区碰撞体切换：必须在 simulate 之前执行！
    UpdateSectorCollisions(registry);
    
    // 2. 计算重力方向和强度
    CalculateGravity(registry);
    
    // 3. 应用重力到 PhysX actor
    ApplyGravityForces(registry);
    
    // 4. 同步 Kinematic 物体
    SyncKinematicToPhysX(registry);
}

void SectorPhysicsSystem::PostPhysicsUpdate(float deltaTime, entt::registry& registry) {
    // 从 PhysX 读取 Dynamic 物体位置
    SyncDynamicFromPhysX(registry);
    
    // 应用渐进式速度补偿（扇区切换时的平滑过渡）
    
    // 飞船稳定化：当飞船接近地面且速度低时，让它sleep
    
    // 每10秒打印当前扇区状态
    static float sectorInfoTimer = 0.0f;
    sectorInfoTimer += deltaTime;
    if (sectorInfoTimer >= 10.0f) {
        sectorInfoTimer = 0.0f;
        PrintCurrentSectorInfo(registry);
    }
    
    // 调试输出：每秒打印一次物理状态（可选，目前禁用）
    // static float debugTimer = 0.0f;
    // debugTimer += deltaTime;
    // if (debugTimer >= 1.0f) {
    //     debugTimer = 0.0f;
    //     PrintPhysicsDebugInfo(registry);
    // }
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

        // Gravity is derived every frame. Never carry a previous sector's
        // source or force into sectorless space when no source is in range.
        affected.currentGravitySource = entt::null;
        affected.currentGravityStrength = 0.0f;
        affected.currentGravityDir = {0.0f, 0.0f, 0.0f};
        
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
        if (dynamicActor->isSleeping()) continue;
        
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
                // 组合旋转：实体局部旋转 + 扇区世界旋转
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
    
    static int syncDebugFrame = 0;
    syncDebugFrame++;
    
    // Debug: 第一帧详细输出
    static bool firstSyncFrame = true;
    
    for (auto entity : view) {
        auto& inSector = view.get<InSectorComponent>(entity);
        auto& transform = view.get<TransformComponent>(entity);
        
        if (inSector.sector == entt::null) {
            // Outside a sector, PhysX and InSector use world-space coordinates.
            transform.position = inSector.localPosition;
            transform.rotation = inSector.localRotation;
            continue;
        }
        
        auto* sector = registry.try_get<SectorComponent>(inSector.sector);
        if (!sector) continue;
        
       
        // 使用扇区的世界位置和旋转来计算实体的世界坐标
        transform.position = LocalToWorld(inSector.localPosition, 
                                           sector->worldPosition, 
                                           sector->worldRotation);
        transform.rotation = CombineRotations(inSector.localRotation, sector->worldRotation);
        
        
    
    }
    
    firstSyncFrame = false;
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

void SectorPhysicsSystem::UpdateSectorCollisions(entt::registry& registry) {
    // 找到需要激活碰撞的扇区
    // 优先使用飞船驾驶时的飞船扇区，否则使用玩家扇区
    entt::entity targetSector = entt::null;
    bool pilotedSpacecraftFound = false;
    
    // 先检查是否有正在被驾驶的飞船
    auto spacecraftView = registry.view<SpacecraftComponent, InSectorComponent>();
    for (auto entity : spacecraftView) {
        auto& spacecraft = spacecraftView.get<SpacecraftComponent>(entity);
        auto& inSector = spacecraftView.get<InSectorComponent>(entity);
        if (spacecraft.currentState == SpacecraftComponent::State::PILOTED) {
            pilotedSpacecraftFound = true;
            targetSector = inSector.sector;
            break;
        }
    }
    
    // 如果没有驾驶飞船，使用玩家扇区
    if (!pilotedSpacecraftFound) {
        auto playerView = registry.view<GravityAffectedComponent, InSectorComponent>(entt::exclude<SpacecraftComponent>);
        for (auto entity : playerView) {
            auto& inSector = playerView.get<InSectorComponent>(entity);
            if (inSector.sector != entt::null) {
                targetSector = inSector.sector;
                break;
            }
        }
    }
    
    // 如果没有找到目标扇区，或者扇区没有变化，直接返回
    if (targetSector == m_ActiveCollisionSector) return;
    
    // 获取扇区名称用于日志
    std::string oldName = "null";
    std::string newName = "space";
    if (m_ActiveCollisionSector != entt::null) {
        auto* oldSector = registry.try_get<SectorComponent>(m_ActiveCollisionSector);
        if (oldSector) oldName = oldSector->name;
    }
    auto* newSectorComp = registry.try_get<SectorComponent>(targetSector);
    if (newSectorComp) newName = newSectorComp->name;
    
    DebugManager::GetInstance().Info(
        "SectorCollision", "Active collision sector: " + oldName + " -> " + newName);
    
    // 第一次初始化：禁用所有扇区碰撞体，除了目标扇区
    if (m_ActiveCollisionSector == entt::null) {
        std::cout << "[SectorPhysics] First-time collision initialization..." << std::endl;
        auto sectorView = registry.view<SectorComponent>();
        for (auto sectorEntity : sectorView) {
            if (sectorEntity == targetSector) {
                SetSectorCollisionEnabled(registry, sectorEntity, true);
            } else {
                SetSectorCollisionEnabled(registry, sectorEntity, false);
            }
        }
    } else {
        // 正常切换：禁用旧扇区，启用新扇区
        SetSectorCollisionEnabled(registry, m_ActiveCollisionSector, false);
        SetSectorCollisionEnabled(registry, targetSector, true);
    }
    
    // 更新当前激活的扇区
    m_ActiveCollisionSector = targetSector;
}

void SectorPhysicsSystem::SetSectorCollisionEnabled(entt::registry& registry, entt::entity sector, bool enabled) {
    auto* sectorComp = registry.try_get<SectorComponent>(sector);
    if (!sectorComp) return;
    if (!sectorComp->physxGround) return;
    
    // 获取 PxRigidStatic 的所有 shape
    physx::PxShape* shapes[8];
    physx::PxU32 numShapes = sectorComp->physxGround->getShapes(shapes, 8);
    
    for (physx::PxU32 i = 0; i < numShapes; i++) {
        // 启用或禁用碰撞检测
        shapes[i]->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, enabled);
        shapes[i]->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE, enabled);
    }
    
    std::ostringstream collisionState;
    collisionState << sectorComp->name << " collision "
                   << (enabled ? "ENABLED" : "DISABLED")
                   << " (" << numShapes << " shapes)";
    DebugManager::GetInstance().Info("SectorCollision", collisionState.str());
}

void SectorPhysicsSystem::CheckAndSwitchSectors(float deltaTime, entt::registry& registry) {
    // 检查所有带 InSectorComponent 的实体，根据世界坐标判断是否需要切换扇区
    auto view = registry.view<InSectorComponent, TransformComponent>();
    auto sectorView = registry.view<SectorComponent>();
    
    // 滞后系数：进入新扇区需要深入一定距离，退出当前扇区需要超出边界
    constexpr float ENTER_HYSTERESIS = 0.92f;  // 进入阈值：需要深入到 influenceRadius * 0.92
    constexpr float EXIT_HYSTERESIS = 1.08f;   // 退出阈值：需要超出 influenceRadius * 1.08
    
    // deltaTime 估计（用于冷却时间）
    
    for (auto entity : view) {
        auto& inSector = view.get<InSectorComponent>(entity);
        auto& transform = view.get<TransformComponent>(entity);
        
        // 更新冷却时间
        if (inSector.switchCooldown > 0.0f) {
            inSector.switchCooldown = (std::max)(0.0f, inSector.switchCooldown - deltaTime);
            if (inSector.switchCooldown > 0.0f) {
                continue;  // 还在冷却中，跳过
            }
        }
        
        // 从 PhysX 获取实际位置
        DirectX::XMFLOAT3 actualLocalPos = inSector.localPosition;
        auto* rigidBody = registry.try_get<RigidBodyComponent>(entity);
        if (rigidBody && rigidBody->physxActor) {
            auto* dynamicActor = rigidBody->physxActor->is<physx::PxRigidDynamic>();
            if (dynamicActor) {
                physx::PxTransform pose = dynamicActor->getGlobalPose();
                actualLocalPos = { pose.p.x, pose.p.y, pose.p.z };
            }
        }
        
        // 计算世界坐标
        DirectX::XMFLOAT3 worldPos = transform.position;
        if (inSector.sector != entt::null) {
            auto* currentSector = registry.try_get<SectorComponent>(inSector.sector);
            if (currentSector) {
                worldPos = LocalToWorld(actualLocalPos, 
                                        currentSector->worldPosition, 
                                        currentSector->worldRotation);
            }
        }
        
        // 找到当前实体应该在的最佳扇区（带滞后机制）
        entt::entity bestSector = FindBestSectorForEntity(registry, worldPos, inSector.sector, 
                                                           ENTER_HYSTERESIS, EXIT_HYSTERESIS);
        
        // 如果找不到合适的扇区，保持当前扇区
        
        // 如果扇区发生变化，执行切换
        if (bestSector != inSector.sector) {
            entt::entity oldSector = inSector.sector;
            
            // 获取扇区名称用于日志
            std::string oldName = "null";
            std::string newName = "space";
            if (oldSector != entt::null) {
                auto* oldSectorComp = registry.try_get<SectorComponent>(oldSector);
                if (oldSectorComp) oldName = oldSectorComp->name;
            }
            auto* newSectorComp = registry.try_get<SectorComponent>(bestSector);
            if (newSectorComp) newName = newSectorComp->name;
            
            std::ostringstream transition;
            transition << "Entity " << static_cast<uint32_t>(entity)
                       << " switching sector: " << oldName << " -> " << newName;
            DebugManager::GetInstance().Info("SectorTransition", transition.str());
            
            // 转移 PhysX Actor 到新扇区
            TransferPhysXActorToSector(registry, entity, oldSector, bestSector);
            
            // 更新 InSectorComponent
            inSector.sector = bestSector;
            inSector.needsSync = true;
            
            // 设置切换冷却时间，防止快速振荡
            inSector.switchCooldown = InSectorComponent::SWITCH_COOLDOWN_DURATION;
            
            // 更新重力源
            auto* gravityAffected = registry.try_get<GravityAffectedComponent>(entity);
            if (gravityAffected) {
                gravityAffected->currentGravitySource = bestSector;
            }
        }
    }
}

entt::entity SectorPhysicsSystem::FindBestSectorForEntity(entt::registry& registry, const DirectX::XMFLOAT3& worldPos,
                                                           entt::entity currentSector, float enterHysteresis, float exitHysteresis) {
    // 带滞后机制的扇区选择
    entt::entity bestSector = entt::null;
    int bestPriority = -999999;
    float bestDistance = FLT_MAX;
    
    auto sectorView = registry.view<SectorComponent>();
    
    for (auto sectorEntity : sectorView) {
        auto& sector = sectorView.get<SectorComponent>(sectorEntity);
        if (!sector.isActive) continue;
        
        // 计算实体到扇区中心的距离
        float dx = worldPos.x - sector.worldPosition.x;
        float dy = worldPos.y - sector.worldPosition.y;
        float dz = worldPos.z - sector.worldPosition.z;
        float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
        
        // 滞后机制：
        // - 对于当前扇区：使用宽松的退出阈值（更难离开）
        // - 对于其他扇区：使用严格的进入阈值（更难进入）
        float threshold;
        if (sectorEntity == currentSector) {
            threshold = sector.influenceRadius * exitHysteresis;  // 退出阈值更大
        } else {
            threshold = sector.influenceRadius * enterHysteresis; // 进入阈值更小
        }
        
        // 检查是否在扇区影响范围内
        if (distance <= threshold) {
            // 在范围内，比较优先级
            // 优先级高的优先，优先级相同时距离近的优先
            if (sector.priority > bestPriority || 
                (sector.priority == bestPriority && distance < bestDistance)) {
                bestPriority = sector.priority;
                bestDistance = distance;
                bestSector = sectorEntity;
            }
        }
    }
    
    return bestSector;
}

void SectorPhysicsSystem::TransferPhysXActorToSector(entt::registry& registry, entt::entity entity,
                                                      entt::entity oldSector, entt::entity newSector) {
    auto* inSector = registry.try_get<InSectorComponent>(entity);
    auto* transform = registry.try_get<TransformComponent>(entity);
    auto* rigidBody = registry.try_get<RigidBodyComponent>(entity);
    
    if (!inSector || !transform) return;
    
    // 获取新扇区信息
    auto* newSectorComp = (newSector != entt::null) ? registry.try_get<SectorComponent>(newSector) : nullptr;
    if (newSector != entt::null && !newSectorComp) return;
    
    // 获取旧扇区信息
    auto* oldSectorComp = (oldSector != entt::null) ? registry.try_get<SectorComponent>(oldSector) : nullptr;
    
    // 从 PhysX 获取实际位置（而不是可能过时的 inSector->localPosition）
    DirectX::XMFLOAT3 actualLocalPos = inSector->localPosition;
    if (rigidBody && rigidBody->physxActor) {
        auto* dynamicActor = rigidBody->physxActor->is<physx::PxRigidDynamic>();
        if (dynamicActor) {
            physx::PxTransform pose = dynamicActor->getGlobalPose();
            actualLocalPos = { pose.p.x, pose.p.y, pose.p.z };
        }
    }
    
    // 计算正确的世界坐标
    DirectX::XMFLOAT3 worldPos;
    if (oldSectorComp) {
        worldPos = LocalToWorld(actualLocalPos, 
                                oldSectorComp->worldPosition, 
                                oldSectorComp->worldRotation);
    } else {
        worldPos = transform->position;
    }
    
    // 计算新的局部坐标
    DirectX::XMFLOAT3 newLocalPos = worldPos;
    if (newSectorComp) {
        newLocalPos = WorldToLocal(worldPos,
                                   newSectorComp->worldPosition,
                                   newSectorComp->worldRotation);
    }
    
    // 更新 InSectorComponent 的局部坐标
    inSector->localPosition = newLocalPos;

    if (auto* character = registry.try_get<CharacterControllerComponent>(entity);
        character && character->pxController) {
        character->pxController->setPosition(physx::PxExtendedVec3(
            newLocalPos.x, newLocalPos.y, newLocalPos.z));
    }

    auto* dynamicActor = (rigidBody && rigidBody->physxActor)
        ? rigidBody->physxActor->is<physx::PxRigidDynamic>()
        : nullptr;
    if (!dynamicActor) {
        const DirectX::XMFLOAT4 oldSectorRot = oldSectorComp
            ? oldSectorComp->worldRotation
            : DirectX::XMFLOAT4{0, 0, 0, 1};
        const DirectX::XMFLOAT4 newSectorRot = newSectorComp
            ? newSectorComp->worldRotation
            : DirectX::XMFLOAT4{0, 0, 0, 1};
        const DirectX::XMVECTOR worldRotation = DirectX::XMQuaternionMultiply(
            DirectX::XMLoadFloat4(&inSector->localRotation),
            DirectX::XMLoadFloat4(&oldSectorRot));
        DirectX::XMVECTOR newLocalRotation = DirectX::XMQuaternionMultiply(
            worldRotation,
            DirectX::XMQuaternionInverse(DirectX::XMLoadFloat4(&newSectorRot)));
        newLocalRotation = DirectX::XMQuaternionNormalize(newLocalRotation);
        DirectX::XMStoreFloat4(&inSector->localRotation, newLocalRotation);
    }
    
    // 如果有 PhysX 刚体，更新其位置
    if (rigidBody && rigidBody->physxActor) {
        if (dynamicActor) {
            // 获取当前速度（相对于旧扇区的局部速度）
            physx::PxVec3 currentVel = dynamicActor->getLinearVelocity();
            physx::PxVec3 angularVel = dynamicActor->getAngularVelocity();
            physx::PxQuat currentLocalRot = dynamicActor->getGlobalPose().q;
            
            // 计算扇区速度差
            DirectX::XMFLOAT3 oldSectorVel = oldSectorComp ? oldSectorComp->worldVelocity : DirectX::XMFLOAT3{0,0,0};
            DirectX::XMFLOAT3 newSectorVel = newSectorComp
                ? newSectorComp->worldVelocity
                : DirectX::XMFLOAT3{0, 0, 0};
            DirectX::XMFLOAT4 oldSectorRot = oldSectorComp ? oldSectorComp->worldRotation : DirectX::XMFLOAT4{0,0,0,1};
            DirectX::XMFLOAT4 newSectorRot = newSectorComp
                ? newSectorComp->worldRotation
                : DirectX::XMFLOAT4{0, 0, 0, 1};
            
            // ====== 旋转转换 ======
            // 旧局部旋转 -> 世界旋转 -> 新局部旋转
            // worldRot = oldSectorRot * oldLocalRot
            // newLocalRot = inverse(newSectorRot) * worldRot
            DirectX::XMFLOAT4 oldLocalRotF4 = { currentLocalRot.x, currentLocalRot.y, currentLocalRot.z, currentLocalRot.w };
            DirectX::XMVECTOR oldLocalRotQ = DirectX::XMLoadFloat4(&oldLocalRotF4);
            DirectX::XMVECTOR oldSectorRotQ = DirectX::XMLoadFloat4(&oldSectorRot);
            DirectX::XMVECTOR newSectorRotQ = DirectX::XMLoadFloat4(&newSectorRot);
            
            // 世界旋转 = 旧扇区旋转 * 旧局部旋转
            DirectX::XMVECTOR worldRotQ = DirectX::XMQuaternionMultiply(oldLocalRotQ, oldSectorRotQ);
            
            // 新局部旋转 = 新扇区旋转的逆 * 世界旋转
            DirectX::XMVECTOR invNewSectorRotQ = DirectX::XMQuaternionInverse(newSectorRotQ);
            DirectX::XMVECTOR newLocalRotQ = DirectX::XMQuaternionMultiply(worldRotQ, invNewSectorRotQ);
            newLocalRotQ = DirectX::XMQuaternionNormalize(newLocalRotQ);
            
            DirectX::XMFLOAT4 newLocalRotF4;
            DirectX::XMStoreFloat4(&newLocalRotF4, newLocalRotQ);
            physx::PxQuat newLocalRot(newLocalRotF4.x, newLocalRotF4.y, newLocalRotF4.z, newLocalRotF4.w);
            
            // 更新 InSectorComponent 的局部旋转
            inSector->localRotation = newLocalRotF4;
            
            // ====== 简化速度继承：只保留飞船速度，不考虑扇区速度差 ======
            // 只需要将速度从旧扇区坐标系旋转到新扇区坐标系
            DirectX::XMFLOAT3 localVel = { currentVel.x, currentVel.y, currentVel.z };
            DirectX::XMVECTOR localVelVec = DirectX::XMLoadFloat3(&localVel);
            
            // 旧扇区局部速度 -> 世界速度（旋转）
            DirectX::XMVECTOR oldRotQuat = DirectX::XMLoadFloat4(&oldSectorRot);
            DirectX::XMVECTOR worldVelVec = DirectX::XMVector3Rotate(localVelVec, oldRotQuat);
            worldVelVec = DirectX::XMVectorAdd(worldVelVec, DirectX::XMLoadFloat3(&oldSectorVel));
            
            // 世界速度 -> 新扇区局部速度（逆旋转）
            DirectX::XMVECTOR newRotQuat = DirectX::XMLoadFloat4(&newSectorRot);
            DirectX::XMVECTOR invNewRotQuat = DirectX::XMQuaternionInverse(newRotQuat);
            worldVelVec = DirectX::XMVectorSubtract(worldVelVec, DirectX::XMLoadFloat3(&newSectorVel));
            DirectX::XMVECTOR newLocalVelVec = DirectX::XMVector3Rotate(worldVelVec, invNewRotQuat);
            
            DirectX::XMFLOAT3 newLocalVel;
            DirectX::XMStoreFloat3(&newLocalVel, newLocalVelVec);
            
            physx::PxVec3 newLinearVel(newLocalVel.x, newLocalVel.y, newLocalVel.z);
            DirectX::XMFLOAT3 localAngularVel = { angularVel.x, angularVel.y, angularVel.z };
            DirectX::XMVECTOR worldAngularVel = DirectX::XMVector3Rotate(
                DirectX::XMLoadFloat3(&localAngularVel), oldRotQuat);
            DirectX::XMVECTOR newLocalAngularVelVec = DirectX::XMVector3Rotate(
                worldAngularVel, invNewRotQuat);
            DirectX::XMFLOAT3 newLocalAngularVel;
            DirectX::XMStoreFloat3(&newLocalAngularVel, newLocalAngularVelVec);
            
            // 调试输出（使用已有的 oldSectorVel 和 newSectorVel）
            std::ostringstream velocityTransition;
            velocityTransition << "Velocity frame change: local speed " << currentVel.magnitude()
                               << " -> " << newLinearVel.magnitude();
            DebugManager::GetInstance().Info("SectorTransition", velocityTransition.str());
            
            // 设置新的位置和旋转
            physx::PxTransform newPose(
                physx::PxVec3(newLocalPos.x, newLocalPos.y, newLocalPos.z),
                newLocalRot  // 使用转换后的新局部旋转
            );
            dynamicActor->setGlobalPose(newPose);
            
            // 直接设置速度（无渐进过渡）
            dynamicActor->setLinearVelocity(newLinearVel);
            dynamicActor->setAngularVelocity(physx::PxVec3(
                newLocalAngularVel.x, newLocalAngularVel.y, newLocalAngularVel.z));
            
            dynamicActor->wakeUp();
            
            DebugManager::GetInstance().Info("SectorTransition", "Coordinate frame transfer complete");
        }
    }
}

void SectorPhysicsSystem::PrintCurrentSectorInfo(entt::registry& registry) {
    std::cout << "\n========== [Sector Status Report - Every 30s] ==========" << std::endl;
    
    // 打印所有扇区信息
    std::cout << "-- All Sectors --" << std::endl;
    auto sectorView = registry.view<SectorComponent>();
    for (auto sectorEntity : sectorView) {
        auto& sector = sectorView.get<SectorComponent>(sectorEntity);
        // 检查碰撞是否启用：碰撞启用当且仅当该扇区是当前激活碰撞扇区
        bool collisionEnabled = (sectorEntity == m_ActiveCollisionSector);
        std::cout << "  [" << sector.name << "] priority=" << sector.priority 
                  << ", radius=" << sector.planetRadius
                  << ", influence=" << sector.influenceRadius
                  << ", pos=(" << sector.worldPosition.x << ", " << sector.worldPosition.y << ", " << sector.worldPosition.z << ")"
                  << ", collision=" << (collisionEnabled ? "ON" : "off")
                  << std::endl;
    }
    
    // 打印当前激活的碰撞扇区
    if (m_ActiveCollisionSector != entt::null) {
        auto* activeSector = registry.try_get<SectorComponent>(m_ActiveCollisionSector);
        if (activeSector) {
            std::cout << "-- Active Collision Sector: " << activeSector->name << " --" << std::endl;
        }
    }
    
    // 打印所有实体的扇区归属
    std::cout << "-- Entities in Sectors --" << std::endl;
    auto entityView = registry.view<InSectorComponent, TransformComponent>();
    for (auto entity : entityView) {
        auto& inSector = entityView.get<InSectorComponent>(entity);
        auto& transform = entityView.get<TransformComponent>(entity);
        
        std::string sectorName = "null";
        if (inSector.sector != entt::null) {
            auto* sector = registry.try_get<SectorComponent>(inSector.sector);
            if (sector) sectorName = sector->name;
        }
        
        // 判断实体类型
        std::string entityType = "Unknown";
        if (registry.all_of<SpacecraftComponent>(entity)) {
            entityType = "Spacecraft";
        } else if (registry.all_of<GravityAffectedComponent>(entity) && !registry.all_of<SpacecraftComponent>(entity)) {
            entityType = "Player";
        }
        
        std::cout << "  Entity " << static_cast<uint32_t>(entity) << " (" << entityType << ")"
                  << " in sector [" << sectorName << "]"
                  << ", worldPos=(" << transform.position.x << ", " << transform.position.y << ", " << transform.position.z << ")"
                  << ", localPos=(" << inSector.localPosition.x << ", " << inSector.localPosition.y << ", " << inSector.localPosition.z << ")"
                  << std::endl;
        
        // 计算到各扇区的距离
        std::cout << "    Distances to sectors: ";
        for (auto sectorEntity : sectorView) {
            auto& sector = sectorView.get<SectorComponent>(sectorEntity);
            float dx = transform.position.x - sector.worldPosition.x;
            float dy = transform.position.y - sector.worldPosition.y;
            float dz = transform.position.z - sector.worldPosition.z;
            float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
            std::cout << sector.name << "=" << dist << "m ";
            if (dist <= sector.influenceRadius) {
                std::cout << "(IN) ";
            }
        }
        std::cout << std::endl;
    }
    
    std::cout << "======================================================\n" << std::endl;
}

} // namespace outer_wilds
