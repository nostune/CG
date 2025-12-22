#pragma once
#include "../core/ECS.h"
#include "../core/Engine.h"
#include "../graphics/CameraModeSystem.h"
#include "../input/InputManager.h"
#include "../scene/components/TransformComponent.h"
#include "../physics/components/ReferenceFrameComponent.h"
#include "../physics/components/GravityAffectedComponent.h"
#include "../physics/components/RigidBodyComponent.h"
#include "components/SpacecraftComponent.h"
#include "components/CharacterControllerComponent.h"
#include "components/PlayerComponent.h"
#include "../graphics/components/MeshComponent.h"
#include <DirectXMath.h>
#include <PxPhysicsAPI.h>
#include <cfloat>
#include <algorithm>
#include <cmath>

namespace outer_wilds {

/**
 * 处理玩家与飞船的交互：靠近提示、按键登船/离船、座椅同步以及第一人称切换。
 */
class SpacecraftInteractionSystem : public System {
public:
    void Update(float deltaTime, entt::registry& registry) override;

private:
    using Spacecraft = components::SpacecraftComponent;
    using PlayerInteraction = components::PlayerSpacecraftInteractionComponent;
    using CharacterController = components::CharacterControllerComponent;
    using Attached = components::AttachedToReferenceFrameComponent;
    using Gravity = components::GravityAffectedComponent;

    void HandleWalkingState(Entity player,
                           PlayerInteraction& interaction,
                           CharacterController& controller,
                           TransformComponent& playerTransform,
                           bool ePressed,
                           entt::registry& registry);

    void HandlePilotingState(Entity player,
                             PlayerInteraction& interaction,
                             CharacterController& controller,
                             TransformComponent& playerTransform,
                             bool ePressed,
                             entt::registry& registry);

    void EnterSpacecraft(Entity player,
                         Entity spacecraft,
                         PlayerInteraction& interaction,
                         CharacterController& controller,
                         TransformComponent& playerTransform,
                         entt::registry& registry);

    void ExitSpacecraft(Entity player,
                        Entity spacecraft,
                        PlayerInteraction& interaction,
                        CharacterController& controller,
                        TransformComponent& playerTransform,
                        entt::registry& registry);

    void AlignPlayerToCockpit(const Spacecraft& spacecraft,
                              const TransformComponent& spacecraftTransform,
                              CharacterController& controller,
                              TransformComponent& playerTransform) const;

    void InitializeSpacecraftOrientation(Spacecraft& spacecraft,
                                         const TransformComponent& transform) const;

    void UpdateCharacterUpDirection(CharacterController& controller,
                                    const Gravity* playerGravity,
                                    const PlayerInteraction& interaction,
                                    entt::registry& registry) const;

    physx::PxVec3 ComputeReferenceFrameVelocity(const Attached* attached,
                                                entt::registry& registry) const;

    bool m_eWasHeld = false;
};

inline void SpacecraftInteractionSystem::Update(float /*deltaTime*/, entt::registry& registry) {
    auto view = registry.view<PlayerInteraction, CharacterController, TransformComponent>();

    bool eHeld = InputManager::GetInstance().IsKeyHeld('E');
    bool ePressed = !m_eWasHeld && eHeld;
    m_eWasHeld = eHeld;

    for (auto player : view) {
        auto& interaction = view.get<PlayerInteraction>(player);
        auto& controller = view.get<CharacterController>(player);
        auto& transform = view.get<TransformComponent>(player);

        if (interaction.isPiloting && interaction.currentSpacecraft != entt::null) {
            HandlePilotingState(player, interaction, controller, transform, ePressed, registry);
        } else {
            HandleWalkingState(player, interaction, controller, transform, ePressed, registry);
        }

        auto* gravity = registry.try_get<Gravity>(player);
        UpdateCharacterUpDirection(controller, gravity, interaction, registry);
    }
}

inline void SpacecraftInteractionSystem::HandleWalkingState(Entity player,
                                                            PlayerInteraction& interaction,
                                                            CharacterController& controller,
                                                            TransformComponent& playerTransform,
                                                            bool ePressed,
                                                            entt::registry& registry) {
    DirectX::XMVECTOR playerPos = DirectX::XMLoadFloat3(&playerTransform.position);
    auto spacecraftView = registry.view<Spacecraft, TransformComponent>();

    float nearestDistance = FLT_MAX;
    entt::entity nearestSpacecraft = entt::null;

    for (auto spacecraftEntity : spacecraftView) {
        auto& spacecraft = spacecraftView.get<Spacecraft>(spacecraftEntity);
        auto& transform = spacecraftView.get<TransformComponent>(spacecraftEntity);

        if (spacecraft.currentState != Spacecraft::State::IDLE) {
            continue; // 已有人驾驶
        }

        DirectX::XMVECTOR craftPos = DirectX::XMLoadFloat3(&transform.position);
        float distance = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(playerPos, craftPos)));

        if (distance < spacecraft.interactionDistance && distance < nearestDistance) {
            nearestDistance = distance;
            nearestSpacecraft = spacecraftEntity;
        }
    }

    interaction.nearestSpacecraft = nearestSpacecraft;
    interaction.distanceToNearest = nearestDistance;

    if (ePressed && nearestSpacecraft != entt::null) {
        EnterSpacecraft(player, nearestSpacecraft, interaction, controller, playerTransform, registry);
    }
}

inline void SpacecraftInteractionSystem::HandlePilotingState(Entity player,
                                                             PlayerInteraction& interaction,
                                                             CharacterController& controller,
                                                             TransformComponent& playerTransform,
                                                             bool ePressed,
                                                             entt::registry& registry) {
    if (!registry.valid(interaction.currentSpacecraft)) {
        interaction.isPiloting = false;
        interaction.currentSpacecraft = entt::null;
        return;
    }

    auto* spacecraft = registry.try_get<Spacecraft>(interaction.currentSpacecraft);
    auto* transform = registry.try_get<TransformComponent>(interaction.currentSpacecraft);

    if (!spacecraft || !transform) {
        interaction.isPiloting = false;
        interaction.currentSpacecraft = entt::null;
        return;
    }

    AlignPlayerToCockpit(*spacecraft, *transform, controller, playerTransform);

    if (ePressed) {
        ExitSpacecraft(player, interaction.currentSpacecraft, interaction, controller, playerTransform, registry);
    }
}

inline void SpacecraftInteractionSystem::EnterSpacecraft(Entity player,
                                                         Entity spacecraftEntity,
                                                         PlayerInteraction& interaction,
                                                         CharacterController& controller,
                                                         TransformComponent& playerTransform,
                                                         entt::registry& registry) {
    auto* spacecraft = registry.try_get<Spacecraft>(spacecraftEntity);
    auto* transform = registry.try_get<TransformComponent>(spacecraftEntity);
    auto* rigidBody = registry.try_get<RigidBodyComponent>(spacecraftEntity);
    auto* attached = registry.try_get<Attached>(spacecraftEntity);

    if (!spacecraft || !transform) {
        return;
    }

    interaction.currentSpacecraft = spacecraftEntity;
    interaction.isPiloting = true;

    spacecraft->currentState = Spacecraft::State::PILOTED;
    spacecraft->pilot = player;

    std::cout << "[SpacecraftInteraction] Player entered spacecraft!" << std::endl;
    std::cout << "  - Setting state to PILOTED" << std::endl;

    if (attached) {
        attached->isSpacecraft = true;
        attached->staticAttach = true;
        attached->groundStableTime = 0.0f;
        std::cout << "  - staticAttach=true, waiting for liftoff input" << std::endl;
    }

    if (rigidBody && rigidBody->physxActor) {
        if (auto* dynamicActor = rigidBody->physxActor->is<physx::PxRigidDynamic>()) {
            // 先清零速度（在非 kinematic 状态下）
            if (!(dynamicActor->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)) {
                dynamicActor->setLinearVelocity(physx::PxVec3(0.0f, 0.0f, 0.0f));
                dynamicActor->setAngularVelocity(physx::PxVec3(0.0f, 0.0f, 0.0f));
            }
            dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
            rigidBody->isKinematic = true;
            std::cout << "  - Set to KINEMATIC mode" << std::endl;
        }
    }

    AlignPlayerToCockpit(*spacecraft, *transform, controller, playerTransform);
    InitializeSpacecraftOrientation(*spacecraft, *transform);

    // 隐藏玩家可视化模型
    auto* playerComp = registry.try_get<PlayerComponent>(player);
    if (playerComp && playerComp->visualModelEntity != entt::null) {
        auto* meshComp = registry.try_get<components::MeshComponent>(playerComp->visualModelEntity);
        if (meshComp) {
            meshComp->isVisible = false;
            std::cout << "  - Player visual model hidden" << std::endl;
        }
    }

    if (auto* cameraMode = Engine::GetInstance().GetCameraModeSystem()) {
        cameraMode->SwitchToSpacecraftMode(registry, spacecraftEntity);
    }
}

inline void SpacecraftInteractionSystem::ExitSpacecraft(Entity player,
                                                        Entity spacecraftEntity,
                                                        PlayerInteraction& interaction,
                                                        CharacterController& controller,
                                                        TransformComponent& playerTransform,
                                                        entt::registry& registry) {
    auto* spacecraft = registry.try_get<Spacecraft>(spacecraftEntity);
    auto* transform = registry.try_get<TransformComponent>(spacecraftEntity);

    if (!spacecraft || !transform) {
        interaction.currentSpacecraft = entt::null;
        interaction.isPiloting = false;
        return;
    }

    DirectX::XMVECTOR shipPos = DirectX::XMLoadFloat3(&transform->position);
    DirectX::XMVECTOR shipRot = DirectX::XMLoadFloat4(&transform->rotation);
    DirectX::XMVECTOR exitOffset = DirectX::XMLoadFloat3(&spacecraft->exitOffset);
    exitOffset = DirectX::XMVector3Rotate(exitOffset, shipRot);
    DirectX::XMVECTOR exitPos = DirectX::XMVectorAdd(shipPos, exitOffset);
    DirectX::XMStoreFloat3(&playerTransform.position, exitPos);

    if (controller.controller) {
        controller.controller->setFootPosition(physx::PxExtendedVec3(
            playerTransform.position.x,
            playerTransform.position.y,
            playerTransform.position.z));
    }

    interaction.currentSpacecraft = entt::null;
    interaction.isPiloting = false;
    interaction.nearestSpacecraft = entt::null;
    interaction.distanceToNearest = FLT_MAX;

    spacecraft->currentState = Spacecraft::State::IDLE;
    spacecraft->pilot = entt::null;

    // 显示玩家可视化模型
    auto* playerComp = registry.try_get<PlayerComponent>(player);
    if (playerComp && playerComp->visualModelEntity != entt::null) {
        auto* meshComp = registry.try_get<components::MeshComponent>(playerComp->visualModelEntity);
        if (meshComp) {
            meshComp->isVisible = true;
            std::cout << "[SpacecraftInteraction] Player visual model shown" << std::endl;
        }
    }

    if (auto* cameraMode = Engine::GetInstance().GetCameraModeSystem()) {
        cameraMode->ExitSpacecraftMode(registry);
    }
}

inline void SpacecraftInteractionSystem::AlignPlayerToCockpit(
    const Spacecraft& spacecraft,
    const TransformComponent& spacecraftTransform,
    CharacterController& controller,
    TransformComponent& playerTransform) const {

    DirectX::XMVECTOR shipPos = DirectX::XMLoadFloat3(&spacecraftTransform.position);
    DirectX::XMVECTOR shipRot = DirectX::XMLoadFloat4(&spacecraftTransform.rotation);
    DirectX::XMVECTOR offset = DirectX::XMLoadFloat3(&spacecraft.cameraOffset);
    offset = DirectX::XMVector3Rotate(offset, shipRot);
    DirectX::XMVECTOR seatPos = DirectX::XMVectorAdd(shipPos, offset);

    DirectX::XMStoreFloat3(&playerTransform.position, seatPos);

    if (controller.controller) {
        controller.controller->setFootPosition(physx::PxExtendedVec3(
            playerTransform.position.x,
            playerTransform.position.y,
            playerTransform.position.z));
    }
}

inline void SpacecraftInteractionSystem::InitializeSpacecraftOrientation(
    Spacecraft& spacecraft,
    const TransformComponent& transform) const {

    DirectX::XMVECTOR rot = DirectX::XMLoadFloat4(&transform.rotation);
    DirectX::XMVECTOR forward = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), rot);
    DirectX::XMFLOAT3 fwd;
    DirectX::XMStoreFloat3(&fwd, DirectX::XMVector3Normalize(forward));

    spacecraft.yaw = std::atan2f(fwd.x, fwd.z);
    spacecraft.pitch = std::asinf(-fwd.y);
    spacecraft.currentYawSpeed = 0.0f;
    spacecraft.currentPitchSpeed = 0.0f;
}

inline void SpacecraftInteractionSystem::UpdateCharacterUpDirection(
    CharacterController& controller,
    const Gravity* playerGravity,
    const PlayerInteraction& interaction,
    entt::registry& registry) const {

    if (!controller.controller) {
        return;
    }

    DirectX::XMFLOAT3 upDir = { 0.0f, 1.0f, 0.0f };

    if (interaction.isPiloting && interaction.currentSpacecraft != entt::null) {
        if (auto* spacecraftGravity = registry.try_get<Gravity>(interaction.currentSpacecraft)) {
            upDir = spacecraftGravity->GetUpDirection();
        } else if (playerGravity) {
            upDir = playerGravity->GetUpDirection();
        }
    } else if (playerGravity) {
        upDir = playerGravity->GetUpDirection();
    }

    DirectX::XMVECTOR up = DirectX::XMLoadFloat3(&upDir);
    if (DirectX::XMVectorGetX(DirectX::XMVector3Length(up)) < 0.001f) {
        up = DirectX::XMVectorSet(0, 1, 0, 0);
        DirectX::XMStoreFloat3(&upDir, up);
    }

    controller.controller->setUpDirection(physx::PxVec3(upDir.x, upDir.y, upDir.z));
}

inline physx::PxVec3 SpacecraftInteractionSystem::ComputeReferenceFrameVelocity(
    const Attached* attached,
    entt::registry& registry) const {

    if (!attached || attached->referenceFrame == entt::null) {
        return physx::PxVec3(0.0f, 0.0f, 0.0f);
    }

    auto* reference = registry.try_get<components::ReferenceFrameComponent>(attached->referenceFrame);
    if (!reference) {
        return physx::PxVec3(0.0f, 0.0f, 0.0f);
    }

    return physx::PxVec3(reference->velocity.x, reference->velocity.y, reference->velocity.z);
}

} // namespace outer_wilds
