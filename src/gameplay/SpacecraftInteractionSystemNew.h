#pragma once
#include <entt/entt.hpp>
#include <DirectXMath.h>
#include <PxPhysicsAPI.h>
#include "../core/ECS.h"
#include "components/SpacecraftComponent.h"
#include "../scene/components/TransformComponent.h"
#include "../physics/components/RigidBodyComponent.h"
#include "../gameplay/components/CharacterControllerComponent.h"
#include "../physics/components/SectorComponent.h"
#include "../input/InputManager.h"

namespace outer_wilds::systems {

using namespace components;
using namespace DirectX;

/**
 * 飞船交互系统 - 星际拓荒风格
 */
class SpacecraftInteractionSystemNew {
public:
    explicit SpacecraftInteractionSystemNew(entt::registry& registry)
        : m_Registry(registry)
    {}
    
    void Update(float deltaTime) {
        UpdateSpacecraftProximity();
        ProcessInteractionInput();
        SyncPilotPosition();
    }
    
    void SetPlayerEntity(entt::entity player) { m_PlayerEntity = player; }
    entt::entity GetPlayerEntity() const { return m_PlayerEntity; }
    
    bool IsPlayerPiloting() const {
        if (m_PlayerEntity == entt::null) return false;
        auto* interaction = m_Registry.try_get<PlayerSpacecraftInteractionComponent>(m_PlayerEntity);
        return interaction && interaction->isPiloting;
    }

private:
    entt::registry& m_Registry;
    entt::entity m_PlayerEntity = entt::null;
    
    void UpdateSpacecraftProximity() {
        if (m_PlayerEntity == entt::null) return;
        
        auto* playerInteraction = m_Registry.try_get<PlayerSpacecraftInteractionComponent>(m_PlayerEntity);
        auto* playerTransform = m_Registry.try_get<outer_wilds::TransformComponent>(m_PlayerEntity);
        
        if (!playerInteraction || !playerTransform) return;
        
        if (playerInteraction->isPiloting) {
            playerInteraction->showInteractionPrompt = false;
            return;
        }
        
        XMVECTOR playerPos = XMLoadFloat3(&playerTransform->position);
        
        playerInteraction->nearestSpacecraft = entt::null;
        playerInteraction->distanceToNearest = FLT_MAX;
        playerInteraction->showInteractionPrompt = false;
        
        auto view = m_Registry.view<SpacecraftComponent, outer_wilds::TransformComponent>();
        for (auto entity : view) {
            auto& spacecraft = view.get<SpacecraftComponent>(entity);
            auto& scTransform = view.get<outer_wilds::TransformComponent>(entity);
            
            XMVECTOR scPos = XMLoadFloat3(&scTransform.position);
            float distance = XMVectorGetX(XMVector3Length(XMVectorSubtract(scPos, playerPos)));
            
            if (distance < playerInteraction->distanceToNearest) {
                playerInteraction->distanceToNearest = distance;
                playerInteraction->nearestSpacecraft = entity;
            }
        }
        
        if (playerInteraction->nearestSpacecraft != entt::null) {
            auto& nearestSC = m_Registry.get<SpacecraftComponent>(playerInteraction->nearestSpacecraft);
            if (playerInteraction->distanceToNearest < nearestSC.interactionDistance &&
                nearestSC.currentState == SpacecraftComponent::State::IDLE) {
                playerInteraction->showInteractionPrompt = true;
            }
        }
    }
    
    void ProcessInteractionInput() {
        if (m_PlayerEntity == entt::null) return;
        
        auto& inputManager = InputManager::GetInstance();
        
        if (!inputManager.IsKeyPressed('F')) return;  // F键交互
        
        auto* playerInteraction = m_Registry.try_get<PlayerSpacecraftInteractionComponent>(m_PlayerEntity);
        if (!playerInteraction) return;
        
        if (playerInteraction->isPiloting) {
            ExitSpacecraft(m_PlayerEntity, playerInteraction->currentSpacecraft);
        } else if (playerInteraction->showInteractionPrompt) {
            EnterSpacecraft(m_PlayerEntity, playerInteraction->nearestSpacecraft);
        }
    }
    
    void EnterSpacecraft(entt::entity player, entt::entity spacecraft) {
        OutputDebugStringA("[Spacecraft] Player entering spacecraft\n");
        
        auto* playerInteraction = m_Registry.try_get<PlayerSpacecraftInteractionComponent>(player);
        auto* playerController = m_Registry.try_get<CharacterControllerComponent>(player);
        auto* playerSector = m_Registry.try_get<SectorAffiliationComponent>(player);
        
        auto* spacecraftComp = m_Registry.try_get<SpacecraftComponent>(spacecraft);
        auto* scTransform = m_Registry.try_get<outer_wilds::TransformComponent>(spacecraft);
        auto* scRigidBody = m_Registry.try_get<RigidBodyComponent>(spacecraft);
        auto* scSector = m_Registry.try_get<SectorAffiliationComponent>(spacecraft);
        
        if (!playerInteraction || !spacecraftComp || !scTransform || !scRigidBody) {
            return;
        }
        
        playerInteraction->isPiloting = true;
        playerInteraction->currentSpacecraft = spacecraft;
        playerInteraction->showSpeedometer = true;
        
        spacecraftComp->pilot = player;
        spacecraftComp->currentState = SpacecraftComponent::State::PILOTED;
        spacecraftComp->isGrounded = false;
        
        // 飞船进入驾驶状态：保持在当前扇区，由飞船控制系统决定是否切换
        if (scSector) {
            scSector->sectorLocked = false;      // 解锁允许被控制
            scSector->autoSectorSwitch = true;   // 启用自动切换扇区
        }
        
        if (scRigidBody->physxActor) {
            auto* dynamicActor = scRigidBody->physxActor->is<physx::PxRigidDynamic>();
            if (dynamicActor) {
                // 先从 kinematic 切换到 dynamic（必须先切换才能设置速度）
                dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, false);
                scRigidBody->isKinematic = false;
                
                // 然后清除速度，避免被弹飞
                dynamicActor->setLinearVelocity(physx::PxVec3(0, 0, 0));
                dynamicActor->setAngularVelocity(physx::PxVec3(0, 0, 0));
                
                // 唤醒Actor确保物理生效
                dynamicActor->wakeUp();
            }
        }
        
        if (playerController && playerController->controller) {
            XMMATRIX scRotMatrix = XMMatrixRotationQuaternion(XMLoadFloat4(&scTransform->rotation));
            XMVECTOR cockpitOffset = XMLoadFloat3(&spacecraftComp->cockpitOffset);
            XMVECTOR worldCockpitOffset = XMVector3TransformNormal(cockpitOffset, scRotMatrix);
            
            // === 扇区坐标系：使用局部坐标 ===
            // 获取飞船的局部位置（相对于扇区）
            XMFLOAT3 scLocalPos = scSector ? scSector->localPosition : XMFLOAT3{0, 0, 0};
            XMVECTOR cockpitPos = XMVectorAdd(XMLoadFloat3(&scLocalPos), worldCockpitOffset);
            
            XMFLOAT3 newLocalPos;
            XMStoreFloat3(&newLocalPos, cockpitPos);
            
            // PhysX 使用局部坐标
            playerController->controller->setFootPosition(
                physx::PxExtendedVec3(newLocalPos.x, newLocalPos.y - 1.0f, newLocalPos.z));
            
            if (playerSector && scSector) {
                playerSector->currentSector = scSector->currentSector;
                playerSector->localPosition = newLocalPos;
            }
        }
    }
    
    void ExitSpacecraft(entt::entity player, entt::entity spacecraft) {
        OutputDebugStringA("[Spacecraft] Player exiting spacecraft\n");
        
        auto* playerInteraction = m_Registry.try_get<PlayerSpacecraftInteractionComponent>(player);
        auto* playerTransform = m_Registry.try_get<outer_wilds::TransformComponent>(player);
        auto* playerController = m_Registry.try_get<CharacterControllerComponent>(player);
        auto* playerSector = m_Registry.try_get<SectorAffiliationComponent>(player);
        
        auto* spacecraftComp = m_Registry.try_get<SpacecraftComponent>(spacecraft);
        auto* scTransform = m_Registry.try_get<outer_wilds::TransformComponent>(spacecraft);
        auto* scRigidBody = m_Registry.try_get<RigidBodyComponent>(spacecraft);
        auto* scSector = m_Registry.try_get<SectorAffiliationComponent>(spacecraft);
        
        if (!playerInteraction || !spacecraftComp || !scTransform) {
            return;
        }
        
        XMMATRIX scRotMatrix = XMMatrixRotationQuaternion(XMLoadFloat4(&scTransform->rotation));
        XMVECTOR exitOffset = XMLoadFloat3(&spacecraftComp->exitOffset);
        XMVECTOR worldExitOffset = XMVector3TransformNormal(exitOffset, scRotMatrix);
        
        // === 扇区坐标系：使用局部坐标 ===
        // 获取飞船的局部位置（相对于扇区）
        XMFLOAT3 scLocalPos = scSector ? scSector->localPosition : XMFLOAT3{0, 0, 0};
        XMVECTOR exitPos = XMVectorAdd(XMLoadFloat3(&scLocalPos), worldExitOffset);
        
        XMFLOAT3 newLocalPos;
        XMStoreFloat3(&newLocalPos, exitPos);
        
        if (playerController && playerController->controller) {
            // PhysX 使用局部坐标
            playerController->controller->setFootPosition(
                physx::PxExtendedVec3(newLocalPos.x, newLocalPos.y, newLocalPos.z));
            
            // Transform 将由 SectorSystem 更新为世界坐标
        }
        
        if (playerSector && scSector) {
            playerSector->currentSector = scSector->currentSector;
            playerSector->autoSectorSwitch = true;
            playerSector->localPosition = newLocalPos;  // 直接设置局部坐标
        }
        
        playerInteraction->isPiloting = false;
        playerInteraction->currentSpacecraft = entt::null;
        playerInteraction->showSpeedometer = false;
        
        spacecraftComp->pilot = entt::null;
        spacecraftComp->currentState = SpacecraftComponent::State::IDLE;
        
        // 设为 kinematic 前，先清除速度（必须在 dynamic 状态下清除）
        if (scRigidBody && scRigidBody->physxActor) {
            auto* dynamicActor = scRigidBody->physxActor->is<physx::PxRigidDynamic>();
            if (dynamicActor) {
                // 检查当前是否为 dynamic（只有 dynamic 才能设置速度）
                bool isCurrentlyKinematic = (dynamicActor->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC);
                if (!isCurrentlyKinematic) {
                    // 先清除速度
                    dynamicActor->setLinearVelocity(physx::PxVec3(0, 0, 0));
                    dynamicActor->setAngularVelocity(physx::PxVec3(0, 0, 0));
                }
                
                // 然后设为 kinematic
                dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
                scRigidBody->isKinematic = true;
            }
        }
        
        // 飞船退出驾驶状态：锁定到当前扇区，让它固定跟随天体
        if (scSector) {
            scSector->sectorLocked = true;       // 锁定扇区，不再自动切换
            scSector->autoSectorSwitch = false;  // 禁用自动切换
            
            // 更新局部坐标为当前位置
            if (scSector->currentSector != entt::null) {
                auto* sector = m_Registry.try_get<SectorComponent>(scSector->currentSector);
                if (sector) {
                    scSector->localPosition = sector->WorldToLocalPosition(scTransform->position);
                }
            }
        }
    }
    
    void SyncPilotPosition() {
        auto view = m_Registry.view<SpacecraftComponent, outer_wilds::TransformComponent>();
        
        for (auto entity : view) {
            auto& spacecraft = view.get<SpacecraftComponent>(entity);
            
            if (spacecraft.currentState != SpacecraftComponent::State::PILOTED &&
                spacecraft.currentState != SpacecraftComponent::State::LANDING) {
                continue;
            }
            
            if (spacecraft.pilot == entt::null) continue;
            
            auto& scTransform = view.get<outer_wilds::TransformComponent>(entity);
            auto* pilotController = m_Registry.try_get<CharacterControllerComponent>(spacecraft.pilot);
            auto* pilotTransform = m_Registry.try_get<outer_wilds::TransformComponent>(spacecraft.pilot);
            
            if (!pilotController || !pilotController->controller) continue;
            
            // === 扇区坐标系：使用局部坐标 ===
            auto* scSector = m_Registry.try_get<SectorAffiliationComponent>(entity);
            auto* pilotSector = m_Registry.try_get<SectorAffiliationComponent>(spacecraft.pilot);
            
            XMMATRIX scRotMatrix = XMMatrixRotationQuaternion(XMLoadFloat4(&scTransform.rotation));
            XMVECTOR cockpitOffset = XMLoadFloat3(&spacecraft.cockpitOffset);
            XMVECTOR worldCockpitOffset = XMVector3TransformNormal(cockpitOffset, scRotMatrix);
            
            // 获取飞船的局部位置（相对于扇区）
            XMFLOAT3 scLocalPos = scSector ? scSector->localPosition : XMFLOAT3{0, 0, 0};
            XMVECTOR cockpitPos = XMVectorAdd(XMLoadFloat3(&scLocalPos), worldCockpitOffset);
            
            XMFLOAT3 newLocalPos;
            XMStoreFloat3(&newLocalPos, cockpitPos);
            
            // PhysX 使用局部坐标
            pilotController->controller->setFootPosition(
                physx::PxExtendedVec3(newLocalPos.x, newLocalPos.y - 1.0f, newLocalPos.z));
            
            // 更新飞行员的扇区信息
            if (pilotSector && scSector) {
                pilotSector->currentSector = scSector->currentSector;
                pilotSector->localPosition = newLocalPos;
            }
            
            // Transform 将由 SectorSystem 更新为世界坐标
            if (pilotTransform) {
                pilotTransform->rotation = scTransform.rotation;
            }
        }
    }
};

} // namespace outer_wilds::systems
