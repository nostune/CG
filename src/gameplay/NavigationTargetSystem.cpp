#include "NavigationTargetSystem.h"

#include "components/NavigationTargetState.h"
#include "components/SolarMapState.h"
#include "components/SpacecraftComponent.h"
#include "../core/DebugManager.h"
#include "../graphics/components/CameraComponent.h"
#include "../input/InputManager.h"
#include "../physics/components/RigidBodyComponent.h"
#include "../physics/components/SectorComponent.h"
#include "../scene/Scene.h"
#include "../scene/components/TransformComponent.h"
#include <algorithm>
#include <cmath>

namespace outer_wilds {

using namespace components;

namespace {

float Length(const DirectX::XMFLOAT3& value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

DirectX::XMFLOAT3 Normalize(const DirectX::XMFLOAT3& value) {
    const float length = Length(value);
    if (length <= 0.0001f) return {0.0f, 0.0f, 1.0f};
    return {value.x / length, value.y / length, value.z / length};
}

float Dot(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

DirectX::XMFLOAT3 Subtract(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

} // namespace

void NavigationTargetSystem::Initialize() {
    DebugManager::GetInstance().Info("NavigationTarget", "Initialized");
}

void NavigationTargetSystem::Initialize(std::shared_ptr<Scene> scene) {
    m_Scene = std::move(scene);
    Initialize();
}

void NavigationTargetSystem::Update(float, entt::registry& registry) {
    if (!registry.ctx().contains<NavigationTargetState>()) {
        registry.ctx().emplace<NavigationTargetState>();
    }
    auto& state = registry.ctx().get<NavigationTargetState>();
    state.hasCandidate = false;
    state.candidateTarget = entt::null;
    state.matchingVelocity = false;

    entt::entity spacecraftEntity = entt::null;
    auto spacecraftView = registry.view<SpacecraftComponent, RigidBodyComponent, InSectorComponent, TransformComponent>();
    for (const auto entity : spacecraftView) {
        if (spacecraftView.get<SpacecraftComponent>(entity).currentState == SpacecraftComponent::State::PILOTED) {
            spacecraftEntity = entity;
            break;
        }
    }
    if (spacecraftEntity == entt::null) return;

    const auto& spacecraftTransform = spacecraftView.get<TransformComponent>(spacecraftEntity);
    CameraComponent* camera = nullptr;
    auto cameraView = registry.view<CameraComponent>();
    for (const auto entity : cameraView) {
        auto& candidate = cameraView.get<CameraComponent>(entity);
        if (candidate.isActive) camera = &candidate;
    }

    if (camera) {
        const DirectX::XMFLOAT3 cameraForward = Normalize(Subtract(camera->target, camera->position));
        float bestAlignment = std::cos(DirectX::XMConvertToRadians(10.0f));
        auto bodyView = registry.view<SectorComponent>();
        for (const auto entity : bodyView) {
            const auto& body = bodyView.get<SectorComponent>(entity);
            const auto toBody = Normalize(Subtract(body.worldPosition, camera->position));
            const float alignment = Dot(cameraForward, toBody);
            if (alignment > bestAlignment) {
                bestAlignment = alignment;
                state.candidateTarget = entity;
                state.candidateWorldPosition = body.worldPosition;
                state.hasCandidate = true;
            }
        }
    }

    const auto* mapView = registry.ctx().find<SolarMapViewState>();
    const bool mapOpen = mapView && mapView->requestedOpen;
    auto& input = InputManager::GetInstance();
    if (!mapOpen && input.IsKeyPressed(VK_LBUTTON)) {
        if (state.hasCandidate && state.lockedTarget != state.candidateTarget) {
            state.lockedTarget = state.candidateTarget;
            state.matchCompletionLogged = false;
            if (const auto* body = registry.try_get<SectorComponent>(state.lockedTarget)) {
                DebugManager::GetInstance().Info("NavigationTarget", "Locked " + body->name);
            }
        } else if (state.lockedTarget != entt::null) {
            state.lockedTarget = entt::null;
            DebugManager::GetInstance().Info("NavigationTarget", "Target lock cleared");
        }
    }

    if (state.lockedTarget == entt::null || !registry.valid(state.lockedTarget)) {
        state.lockedTarget = entt::null;
        state.targetName.clear();
        state.relativeSpeed = 0.0f;
        return;
    }

    const auto* targetSector = registry.try_get<SectorComponent>(state.lockedTarget);
    if (!targetSector) {
        state.lockedTarget = entt::null;
        return;
    }
    state.targetName = targetSector->name;
    state.targetWorldPosition = targetSector->worldPosition;
    state.targetRadius = targetSector->planetRadius;
    state.distance = Length(Subtract(targetSector->worldPosition, spacecraftTransform.position));

    auto& rigidBody = spacecraftView.get<RigidBodyComponent>(spacecraftEntity);
    const auto& inSector = spacecraftView.get<InSectorComponent>(spacecraftEntity);
    auto* actor = rigidBody.physxActor ? rigidBody.physxActor->is<physx::PxRigidDynamic>() : nullptr;
    if (!actor) return;

    DirectX::XMFLOAT3 frameVelocity = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT4 frameRotation = {0.0f, 0.0f, 0.0f, 1.0f};
    if (const auto* currentSector = registry.try_get<SectorComponent>(inSector.sector)) {
        frameVelocity = currentSector->worldVelocity;
        frameRotation = currentSector->worldRotation;
    }
    const auto frameInverseRotation = DirectX::XMQuaternionInverse(DirectX::XMLoadFloat4(&frameRotation));
    const auto targetVelocityInFrame = DirectX::XMVector3Rotate(
        DirectX::XMVectorSubtract(
            DirectX::XMLoadFloat3(&targetSector->worldVelocity),
            DirectX::XMLoadFloat3(&frameVelocity)),
        frameInverseRotation);
    DirectX::XMFLOAT3 targetLocalVelocity;
    DirectX::XMStoreFloat3(&targetLocalVelocity, targetVelocityInFrame);
    state.targetVelocityLocal = targetLocalVelocity;

    const auto currentVelocity = actor->getLinearVelocity();
    state.relativeVelocityLocal = {
        targetLocalVelocity.x - currentVelocity.x,
        targetLocalVelocity.y - currentVelocity.y,
        targetLocalVelocity.z - currentVelocity.z
    };
    state.relativeSpeed = Length(state.relativeVelocityLocal);

    const bool matchRequested = !mapOpen &&
        (input.IsKeyHeld(VK_SPACE) || state.automatedMatchRequest);
    state.matchingVelocity = matchRequested && state.relativeSpeed > 0.05f;

    if (state.matchingVelocity && !m_MatchWasActive) {
        DebugManager::GetInstance().Info(
            "VelocityMatch", "Engaged for " + state.targetName +
            " at " + std::to_string(state.relativeSpeed) + " m/s relative");
    }
    if (state.automatedMatchRequest && state.relativeSpeed < 0.5f && !state.matchCompletionLogged) {
        state.matchCompletionLogged = true;
        DebugManager::GetInstance().Info(
            "VelocityMatch", "Completed for " + state.targetName +
            " at " + std::to_string(state.relativeSpeed) + " m/s relative");
    }
    m_MatchWasActive = state.matchingVelocity;

    auto& diagnostics = DebugManager::GetInstance();
    diagnostics.SetMetric("Target distance", state.distance, "m");
    diagnostics.SetMetric("Target relative speed", state.relativeSpeed, "m/s");
    diagnostics.SetMetric("Velocity match active", state.matchingVelocity ? 1.0 : 0.0);
}

void NavigationTargetSystem::PrePhysicsUpdate(entt::registry& registry) {
    auto* state = registry.ctx().find<NavigationTargetState>();
    if (!state || !state->matchingVelocity) return;

    auto spacecraftView = registry.view<SpacecraftComponent, RigidBodyComponent>();
    for (const auto entity : spacecraftView) {
        const auto& spacecraft = spacecraftView.get<SpacecraftComponent>(entity);
        if (spacecraft.currentState != SpacecraftComponent::State::PILOTED) continue;

        auto& rigidBody = spacecraftView.get<RigidBodyComponent>(entity);
        auto* actor = rigidBody.physxActor
            ? rigidBody.physxActor->is<physx::PxRigidDynamic>()
            : nullptr;
        if (!actor) return;

        const auto currentVelocity = actor->getLinearVelocity();
        DirectX::XMFLOAT3 acceleration = {
            (state->targetVelocityLocal.x - currentVelocity.x) * state->matchResponse,
            (state->targetVelocityLocal.y - currentVelocity.y) * state->matchResponse,
            (state->targetVelocityLocal.z - currentVelocity.z) * state->matchResponse
        };
        const float accelerationLength = Length(acceleration);
        if (accelerationLength > state->matchAcceleration) {
            const float scale = state->matchAcceleration / accelerationLength;
            acceleration = {acceleration.x * scale, acceleration.y * scale, acceleration.z * scale};
        }
        actor->wakeUp();
        actor->addForce(
            physx::PxVec3(acceleration.x, acceleration.y, acceleration.z),
            physx::PxForceMode::eACCELERATION);
        return;
    }
}

} // namespace outer_wilds
