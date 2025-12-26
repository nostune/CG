#pragma once
#include "../core/ECS.h"
#include "components/SectorComponent.h"
#include "components/GravitySourceComponent.h"
#include "components/RigidBodyComponent.h"
#include "../scene/components/TransformComponent.h"
#include "../gameplay/components/PlayerComponent.h"
#include "../gameplay/components/CharacterControllerComponent.h"
#include "../graphics/components/CameraComponent.h"
#include "PhysXManager.h"
#include <DirectXMath.h>
#include <iostream>
#include <vector>
#include <algorithm>

namespace outer_wilds {

/**
 * SectorSystem - 管理 Sector 坐标系统
 * 
 * ============================================================
 * 核心设计目标：让 PhysX 在惯性系中运行
 * ============================================================
 * 
 * 问题：当星球公转时，站在上面的玩家会因为惯性而"掉队"
 * 
 * 解决方案：
 * - PhysX 坐标系原点 = 当前 Sector（星球）中心
 * - 星球在 PhysX 中始终位于 (0,0,0)，是静止的
 * - 所有物理模拟在这个惯性系中进行
 * - 渲染时：worldPos = sector.worldPosition + localPos
 * 
 * ============================================================
 * 坐标系约定（不做 PhysX/DirectX 手性转换）
 * ============================================================
 * 
 * - PhysX 中的位置 = 相对于 Sector 中心的局部坐标 (localPosition)
 * - 渲染用的位置 = Sector 世界位置 + 局部坐标 (worldPosition)
 * - 不做 Z 轴翻转，PhysX 和 DirectX 共享相同数值
 * 
 * ============================================================
 * 更新顺序
 * ============================================================
 * 
 * 1. OrbitSystem: 更新 Sector（天体）的 TransformComponent.position
 * 2. SectorSystem::Update: 
 *    - 同步 sector.worldPosition
 *    - 管理 Sector 归属
 * 3. PhysicsSystem: 在局部坐标系中模拟
 * 4. SectorSystem::PostPhysicsUpdate:
 *    - 从 PhysX 读取 localPosition
 *    - 计算 worldPosition 供渲染使用
 *    - 更新相机的世界坐标
 * 5. RenderSystem: 使用 TransformComponent.position（世界坐标）渲染
 */
class SectorSystem : public System {
public:
    void Update(float deltaTime, entt::registry& registry) override {
        using namespace DirectX;
        using namespace components;
        
        // 1. 同步所有 Sector 的 worldPosition
        SyncSectorWorldPositions(registry);
        
        // 2. 初始化 Sector 物理体（把星球碰撞体移到局部坐标原点）
        InitializeSectorPhysics(registry);
        
        // 3. 找到玩家，确定活跃 Sector
        Entity playerEntity = FindPlayerEntity(registry);
        if (playerEntity == entt::null) return;
        
        auto* playerInSector = registry.try_get<InSectorComponent>(playerEntity);
        if (!playerInSector) return;
        
        // 4. 确定最佳 Sector
        Entity bestSector = DetermineBestSector(registry, playerEntity, *playerInSector);
        
        // 5. 处理 Sector 切换
        if (bestSector != playerInSector->currentSector && bestSector != entt::null) {
            HandleSectorSwitch(registry, playerEntity, *playerInSector, bestSector);
        }
        
        // 6. 设置活跃 Sector
        SetActiveSector(registry, playerInSector->currentSector);
        
        // 6. 初始化未初始化的实体
        InitializeEntitiesInSector(registry);
    }
    
    /**
     * 物理模拟后调用
     * 
     * 职责：
     * 1. 从 PhysX 读取局部坐标（CharacterController 或 RigidBody）
     * 2. 计算世界坐标 = Sector.worldPosition + localPosition
     * 3. 更新 TransformComponent.position（供渲染使用）
     * 4. 更新 CameraComponent.position（供相机使用）
     * 5. 更新玩家可视模型位置
     */
    void PostPhysicsUpdate(entt::registry& registry) {
        using namespace DirectX;
        using namespace components;
        
        // 调试输出（每5秒一次）
        static int debugFrame = 0;
        bool shouldDebug = (++debugFrame % 300 == 0);
        
        auto view = registry.view<InSectorComponent, TransformComponent>();
        
        for (auto entity : view) {
            auto& inSector = view.get<InSectorComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);
            
            if (inSector.currentSector == entt::null) continue;
            
            auto* sector = registry.try_get<SectorComponent>(inSector.currentSector);
            if (!sector) continue;
            
            // === 从 PhysX 读取局部坐标 ===
            bool hasPhysics = false;
            
            // 优先检查 CharacterController
            auto* charController = registry.try_get<CharacterControllerComponent>(entity);
            if (charController && charController->controller) {
                physx::PxExtendedVec3 footPos = charController->controller->getFootPosition();
                inSector.localPosition = XMFLOAT3(
                    static_cast<float>(footPos.x),
                    static_cast<float>(footPos.y),
                    static_cast<float>(footPos.z)
                );
                hasPhysics = true;
                
                if (shouldDebug) {
                    // 计算与星球中心的距离
                    float distance = sqrtf(footPos.x * footPos.x + footPos.y * footPos.y + footPos.z * footPos.z);
                    std::cout << "[SectorSystem] Player local: (" << (float)footPos.x << ", " << (float)footPos.y 
                              << ", " << (float)footPos.z << "), dist=" << distance << std::endl;
                }
            }
            
            // 其次检查 RigidBody
            if (!hasPhysics) {
                auto* rigidBody = registry.try_get<RigidBodyComponent>(entity);
                if (rigidBody && rigidBody->physxActor) {
                    physx::PxTransform pxTransform = rigidBody->physxActor->getGlobalPose();
                    inSector.localPosition = XMFLOAT3(pxTransform.p.x, pxTransform.p.y, pxTransform.p.z);
                    inSector.localRotation = XMFLOAT4(pxTransform.q.x, pxTransform.q.y, 
                                                       pxTransform.q.z, pxTransform.q.w);
                    hasPhysics = true;
                }
            }
            
            // === 计算世界坐标 ===
            // worldPosition = sector.worldPosition + localPosition
            XMVECTOR sectorWorldPos = XMLoadFloat3(&sector->worldPosition);
            XMVECTOR localPos = XMLoadFloat3(&inSector.localPosition);
            XMVECTOR worldPos = XMVectorAdd(sectorWorldPos, localPos);
            XMStoreFloat3(&transform.position, worldPos);
            
            // === 更新相机位置（如果有） ===
            auto* camera = registry.try_get<CameraComponent>(entity);
            if (camera && camera->isActive) {
                // 相机位置也需要转换到世界坐标
                // PlayerSystem 设置的 camera.position 是局部坐标（眼睛位置）
                XMVECTOR cameraLocalPos = XMLoadFloat3(&camera->position);
                XMVECTOR cameraWorldPos = XMVectorAdd(sectorWorldPos, cameraLocalPos);
                XMStoreFloat3(&camera->position, cameraWorldPos);
                
                // camera.target 也需要转换
                XMVECTOR targetLocal = XMLoadFloat3(&camera->target);
                XMVECTOR targetWorld = XMVectorAdd(sectorWorldPos, targetLocal);
                XMStoreFloat3(&camera->target, targetWorld);
            }
            
            // === 更新玩家可视模型位置 ===
            auto* playerComp = registry.try_get<PlayerComponent>(entity);
            if (playerComp && playerComp->visualModelEntity != entt::null) {
                auto* modelTransform = registry.try_get<TransformComponent>(playerComp->visualModelEntity);
                if (modelTransform) {
                    // 模型位置 = 世界坐标（脚位置）
                    modelTransform->position = transform.position;
                }
            }
        }
    }
    
    /**
     * 将世界坐标转换为 Sector 局部坐标
     */
    static DirectX::XMFLOAT3 WorldToLocal(
        const DirectX::XMFLOAT3& worldPos,
        const components::SectorComponent& sector
    ) {
        using namespace DirectX;
        XMVECTOR world = XMLoadFloat3(&worldPos);
        XMVECTOR sectorOrigin = XMLoadFloat3(&sector.worldPosition);
        XMVECTOR local = XMVectorSubtract(world, sectorOrigin);
        XMFLOAT3 result;
        XMStoreFloat3(&result, local);
        return result;
    }
    
    /**
     * 将 Sector 局部坐标转换为世界坐标
     */
    static DirectX::XMFLOAT3 LocalToWorld(
        const DirectX::XMFLOAT3& localPos,
        const components::SectorComponent& sector
    ) {
        using namespace DirectX;
        XMVECTOR local = XMLoadFloat3(&localPos);
        XMVECTOR sectorOrigin = XMLoadFloat3(&sector.worldPosition);
        XMVECTOR world = XMVectorAdd(sectorOrigin, local);
        XMFLOAT3 result;
        XMStoreFloat3(&result, world);
        return result;
    }

private:
    Entity FindPlayerEntity(entt::registry& registry) {
        using namespace components;
        
        // 找玩家
        auto playerView = registry.view<PlayerComponent, InSectorComponent>();
        for (auto entity : playerView) {
            return entity;
        }
        
        // 备选：任何有 InSectorComponent 的实体
        auto anyView = registry.view<InSectorComponent>();
        for (auto entity : anyView) {
            return entity;
        }
        
        return entt::null;
    }
    
    /**
     * 初始化 Sector 的物理体
     * 在局部坐标系中，Sector（星球）应该在原点 (0,0,0)
     */
    void InitializeSectorPhysics(entt::registry& registry) {
        using namespace components;
        
        auto view = registry.view<SectorComponent, RigidBodyComponent>();
        for (auto entity : view) {
            auto& sector = view.get<SectorComponent>(entity);
            auto& rigidBody = view.get<RigidBodyComponent>(entity);
            
            // 只初始化一次
            if (sector.physicsInitialized) continue;
            
            if (rigidBody.physxActor) {
                // 获取当前位置
                physx::PxTransform oldPose = rigidBody.physxActor->getGlobalPose();
                
                // 在局部坐标系中，Sector 应该在原点
                physx::PxTransform newPose(physx::PxVec3(0.0f, 0.0f, 0.0f), oldPose.q);
                
                // 对于 RigidStatic，需要从场景移除再添加才能正确更新碰撞
                auto* pxScene = PhysXManager::GetInstance().GetScene();
                if (pxScene) {
                    pxScene->removeActor(*rigidBody.physxActor);
                    rigidBody.physxActor->setGlobalPose(newPose);
                    pxScene->addActor(*rigidBody.physxActor);
                    
                    // 强制刷新场景查询缓存
                    pxScene->flushQueryUpdates();
                }
                
                sector.physicsInitialized = true;
                
                std::cout << "[SectorSystem] Initialized Sector '" << sector.name 
                          << "' physics at origin (0,0,0)" << std::endl;
            }
        }
    }
    
    void SyncSectorWorldPositions(entt::registry& registry) {
        using namespace components;
        
        auto view = registry.view<SectorComponent, TransformComponent>();
        for (auto entity : view) {
            auto& sector = view.get<SectorComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);
            sector.worldPosition = transform.position;
            sector.worldRotation = transform.rotation;
        }
    }
    
    Entity DetermineBestSector(
        entt::registry& registry,
        Entity entity,
        const components::InSectorComponent& inSector
    ) {
        using namespace DirectX;
        using namespace components;
        
        auto* transform = registry.try_get<TransformComponent>(entity);
        if (!transform) return inSector.currentSector;
        
        XMVECTOR entityWorldPos = XMLoadFloat3(&transform->position);
        
        struct Candidate {
            Entity entity;
            float distance;
            int priority;
        };
        std::vector<Candidate> candidates;
        
        auto sectorView = registry.view<SectorComponent>();
        for (auto sectorEntity : sectorView) {
            auto& sector = sectorView.get<SectorComponent>(sectorEntity);
            
            XMVECTOR sectorPos = XMLoadFloat3(&sector.worldPosition);
            float distance = XMVectorGetX(XMVector3Length(XMVectorSubtract(entityWorldPos, sectorPos)));
            
            if (distance <= sector.influenceRadius) {
                candidates.push_back({sectorEntity, distance, sector.priority});
            }
        }
        
        if (candidates.empty()) return inSector.currentSector;
        
        std::sort(candidates.begin(), candidates.end(), 
            [](const Candidate& a, const Candidate& b) {
                if (a.priority != b.priority) return a.priority > b.priority;
                return a.distance < b.distance;
            });
        
        return candidates[0].entity;
    }
    
    void HandleSectorSwitch(
        entt::registry& registry,
        Entity entity,
        components::InSectorComponent& inSector,
        Entity newSector
    ) {
        using namespace DirectX;
        using namespace components;
        
        auto* transform = registry.try_get<TransformComponent>(entity);
        auto* newSectorComp = registry.try_get<SectorComponent>(newSector);
        if (!transform || !newSectorComp) return;
        
        // 计算新的局部坐标
        XMFLOAT3 newLocalPos = WorldToLocal(transform->position, *newSectorComp);
        
        inSector.previousSector = inSector.currentSector;
        inSector.currentSector = newSector;
        inSector.localPosition = newLocalPos;
        
        // 更新 PhysX 中的位置
        auto* charController = registry.try_get<CharacterControllerComponent>(entity);
        if (charController && charController->controller) {
            charController->controller->setFootPosition(
                physx::PxExtendedVec3(newLocalPos.x, newLocalPos.y, newLocalPos.z)
            );
        }
        
        auto* rigidBody = registry.try_get<RigidBodyComponent>(entity);
        if (rigidBody && rigidBody->physxActor) {
            physx::PxTransform pose = rigidBody->physxActor->getGlobalPose();
            pose.p = physx::PxVec3(newLocalPos.x, newLocalPos.y, newLocalPos.z);
            rigidBody->physxActor->setGlobalPose(pose);
        }
        
        std::cout << "[SectorSystem] Switched to Sector '" << newSectorComp->name 
                  << "', localPos=(" << newLocalPos.x << ", " << newLocalPos.y 
                  << ", " << newLocalPos.z << ")" << std::endl;
    }
    
    void SetActiveSector(entt::registry& registry, Entity activeSector) {
        using namespace components;
        auto view = registry.view<SectorComponent>();
        for (auto entity : view) {
            auto& sector = view.get<SectorComponent>(entity);
            sector.isActive = (entity == activeSector);
        }
    }
    
    void InitializeEntitiesInSector(entt::registry& registry) {
        using namespace DirectX;
        using namespace components;
        
        auto view = registry.view<InSectorComponent, TransformComponent>();
        
        for (auto entity : view) {
            auto& inSector = view.get<InSectorComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);
            
            if (inSector.isInitialized || inSector.currentSector == entt::null) continue;
            
            auto* sector = registry.try_get<SectorComponent>(inSector.currentSector);
            if (!sector) continue;
            
            // 从世界坐标计算局部坐标
            inSector.localPosition = WorldToLocal(transform.position, *sector);
            inSector.isInitialized = true;
            
            // 同步到 PhysX
            auto* charController = registry.try_get<CharacterControllerComponent>(entity);
            if (charController && charController->controller) {
                // 使用 setPosition 设置胶囊体中心位置
                float controllerHeight = 1.6f;
                float controllerRadius = 0.4f;
                float centerHeight = inSector.localPosition.y + controllerHeight * 0.5f + controllerRadius;
                
                charController->controller->setPosition(
                    physx::PxExtendedVec3(
                        inSector.localPosition.x,
                        centerHeight,
                        inSector.localPosition.z
                    )
                );
                
                // 手动更新内部 Actor 位置
                physx::PxRigidDynamic* actor = charController->controller->getActor();
                if (actor) {
                    physx::PxTransform oldActorPose = actor->getGlobalPose();
                    physx::PxTransform newActorPose(
                        physx::PxVec3((float)inSector.localPosition.x, (float)centerHeight, (float)inSector.localPosition.z),
                        oldActorPose.q
                    );
                    actor->setGlobalPose(newActorPose);
                }
                
                std::cout << "[SectorSystem] Player initialized in Sector '" << sector->name 
                          << "' at local (" << inSector.localPosition.x << ", " 
                          << inSector.localPosition.y << ", " << inSector.localPosition.z << ")" << std::endl;
            }
            
            auto* rigidBody = registry.try_get<RigidBodyComponent>(entity);
            if (rigidBody && rigidBody->physxActor) {
                physx::PxTransform pose(
                    physx::PxVec3(inSector.localPosition.x, inSector.localPosition.y, inSector.localPosition.z)
                );
                rigidBody->physxActor->setGlobalPose(pose);
            }
        }
    }
};

} // namespace outer_wilds
