#include "SolarMapCameraSystem.h"

#include "components/CameraComponent.h"
#include "../core/DebugManager.h"
#include "../gameplay/components/SolarMapState.h"
#include <algorithm>
#include <cmath>

namespace outer_wilds {

using namespace components;

namespace {

float SmoothStep(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

DirectX::XMFLOAT3 Lerp(
    const DirectX::XMFLOAT3& from,
    const DirectX::XMFLOAT3& to,
    float amount) {
    DirectX::XMFLOAT3 result;
    DirectX::XMStoreFloat3(
        &result,
        DirectX::XMVectorLerp(DirectX::XMLoadFloat3(&from), DirectX::XMLoadFloat3(&to), amount));
    return result;
}

} // namespace

void SolarMapCameraSystem::Initialize() {
    DebugManager::GetInstance().Info("SolarMapCamera", "Initialized");
}

void SolarMapCameraSystem::Update(float deltaTime, entt::registry& registry) {
    if (!registry.ctx().contains<SolarMapViewState>()) {
        registry.ctx().emplace<SolarMapViewState>();
    }
    auto& viewState = registry.ctx().get<SolarMapViewState>();
    const auto* map = registry.ctx().find<SolarMapState>();
    if (!map || map->bodies.empty()) return;

    if (m_MapCameraEntity == entt::null || !registry.valid(m_MapCameraEntity)) {
        m_MapCameraEntity = registry.create();
        auto& createdCamera = registry.emplace<CameraComponent>(m_MapCameraEntity);
        createdCamera.isActive = false;
        createdCamera.fov = 58.0f;
        createdCamera.nearPlane = 1.0f;
        createdCamera.farPlane = 50000.0f;
    }
    viewState.cameraEntity = m_MapCameraEntity;
    auto& mapCamera = registry.get<CameraComponent>(m_MapCameraEntity);

    CameraComponent* gameplayCamera = nullptr;
    auto cameras = registry.view<CameraComponent>();
    for (const auto entity : cameras) {
        if (entity == m_MapCameraEntity) continue;
        auto& candidate = cameras.get<CameraComponent>(entity);
        if (candidate.isActive) {
            gameplayCamera = &candidate;
        }
    }
    if (!gameplayCamera) return;

    const DirectX::XMFLOAT3 gameplayPosition = gameplayCamera->position;
    const DirectX::XMFLOAT3 gameplayTarget = gameplayCamera->target;
    const DirectX::XMFLOAT3 gameplayUp = gameplayCamera->up;
    const float gameplayFov = gameplayCamera->fov;

    if (viewState.requestedOpen && !m_WasRequestedOpen) {
        m_DisplayedPosition = gameplayPosition;
        m_DisplayedTarget = gameplayTarget;
        m_DisplayedUp = gameplayUp;
        m_DisplayedFov = gameplayFov;
        m_HasDisplayedPose = true;
        DebugManager::GetInstance().Info("SolarMapCamera", "Entering solar map view");
    } else if (!viewState.requestedOpen && m_WasRequestedOpen) {
        DebugManager::GetInstance().Info("SolarMapCamera", "Leaving solar map view");
    }
    m_WasRequestedOpen = viewState.requestedOpen;

    constexpr float transitionSpeed = 2.6f;
    viewState.transition = std::clamp(
        viewState.transition + (viewState.requestedOpen ? 1.0f : -1.0f) * deltaTime * transitionSpeed,
        0.0f,
        1.0f);
    viewState.cameraOverrideActive = viewState.transition > 0.0f;
    if (!viewState.cameraOverrideActive) {
        m_HasDisplayedPose = false;
        return;
    }

    DirectX::XMFLOAT3 solarCenter = map->bodies.front().position;
    for (const auto& body : map->bodies) {
        if (body.name.find("Sun") != std::string::npos) {
            solarCenter = body.position;
            break;
        }
    }
    if (viewState.centerOnSpacecraft && map->hasSpacecraft) {
        solarCenter = map->spacecraftPosition;
    }
    bool hasFocusedBody = false;
    float focusedRadius = 1.0f;
    for (const auto& body : map->bodies) {
        if (body.entity == viewState.focusedBody) {
            solarCenter = body.position;
            focusedRadius = body.radius;
            hasFocusedBody = true;
            break;
        }
    }

    float extent = hasFocusedBody ? focusedRadius * 8.0f : 1.0f;
    if (!hasFocusedBody) {
        for (const auto& body : map->bodies) {
            const float dx = body.position.x - solarCenter.x;
            const float dy = body.position.y - solarCenter.y;
            const float dz = body.position.z - solarCenter.z;
            extent = (std::max)(extent, std::sqrt(dx * dx + dy * dy + dz * dz) + body.radius);
        }
    }

    const float cosPitch = std::cos(viewState.pitch);
    const DirectX::XMFLOAT3 cameraDirection = {
        cosPitch * std::sin(viewState.yaw),
        std::sin(viewState.pitch),
        -cosPitch * std::cos(viewState.yaw)
    };
    const float destinationFov = 58.0f;
    const float mapDistance = extent * 1.3f /
        ((std::max)(std::tan(DirectX::XMConvertToRadians(destinationFov * 0.5f)), 0.1f) *
         (std::max)(viewState.zoom, 0.1f));
    const DirectX::XMFLOAT3 mapPosition = {
        solarCenter.x + cameraDirection.x * mapDistance,
        solarCenter.y + cameraDirection.y * mapDistance,
        solarCenter.z + cameraDirection.z * mapDistance
    };
    const DirectX::XMFLOAT3 mapUp = {0.0f, 1.0f, 0.0f};

    if (!m_HasDisplayedPose) {
        m_DisplayedPosition = mapPosition;
        m_DisplayedTarget = solarCenter;
        m_DisplayedUp = mapUp;
        m_DisplayedFov = destinationFov;
        m_HasDisplayedPose = true;
    }

    const float blend = SmoothStep(viewState.transition);
    const DirectX::XMFLOAT3 desiredPosition = viewState.requestedOpen ? mapPosition : gameplayPosition;
    const DirectX::XMFLOAT3 desiredTarget = viewState.requestedOpen ? solarCenter : gameplayTarget;
    const DirectX::XMFLOAT3 desiredUp = viewState.requestedOpen ? mapUp : gameplayUp;
    const float desiredFov = viewState.requestedOpen ? destinationFov : gameplayFov;
    const float response = 1.0f - std::exp(-8.0f * deltaTime);

    if (viewState.requestedOpen) {
        m_DisplayedPosition = Lerp(m_DisplayedPosition, desiredPosition, response * (0.35f + blend * 0.65f));
        m_DisplayedTarget = Lerp(m_DisplayedTarget, desiredTarget, response);
        m_DisplayedUp = Lerp(m_DisplayedUp, desiredUp, response);
        m_DisplayedFov += (desiredFov - m_DisplayedFov) * response;
    } else {
        const float exitResponse = 1.0f - std::exp(-10.0f * deltaTime);
        m_DisplayedPosition = Lerp(m_DisplayedPosition, desiredPosition, exitResponse);
        m_DisplayedTarget = Lerp(m_DisplayedTarget, desiredTarget, exitResponse);
        m_DisplayedUp = Lerp(m_DisplayedUp, desiredUp, exitResponse);
        m_DisplayedFov += (desiredFov - m_DisplayedFov) * exitResponse;
    }

    mapCamera.position = m_DisplayedPosition;
    mapCamera.target = m_DisplayedTarget;
    mapCamera.up = m_DisplayedUp;
    mapCamera.fov = m_DisplayedFov;
    mapCamera.aspectRatio = gameplayCamera->aspectRatio;
    mapCamera.nearPlane = (std::max)(0.1f, extent * 0.0005f);
    mapCamera.farPlane = (std::max)(50000.0f, extent * 8.0f);

    const auto view = DirectX::XMMatrixLookAtLH(
        DirectX::XMLoadFloat3(&mapCamera.position),
        DirectX::XMLoadFloat3(&mapCamera.target),
        DirectX::XMLoadFloat3(&mapCamera.up));
    const auto projection = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XMConvertToRadians(mapCamera.fov),
        mapCamera.aspectRatio,
        mapCamera.nearPlane,
        mapCamera.farPlane);
    DirectX::XMStoreFloat4x4(&viewState.viewProjection, view * projection);
}

} // namespace outer_wilds
