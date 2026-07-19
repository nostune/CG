#pragma once

#include <DirectXMath.h>
#include <entt/entt.hpp>
#include <string>
#include <vector>

namespace outer_wilds::components {

struct SolarMapBody {
    entt::entity entity = entt::null;
    std::string name;
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
    float radius = 1.0f;
    bool currentSector = false;
};

struct SolarMapPath {
    entt::entity owner = entt::null;
    std::vector<DirectX::XMFLOAT3> points;
    bool dashed = false;
};

struct SolarMapState {
    std::vector<SolarMapBody> bodies;
    std::vector<SolarMapPath> bodyOrbits;
    SolarMapPath predictedSpacecraftPath;
    DirectX::XMFLOAT3 spacecraftPosition = {0.0f, 0.0f, 0.0f};
    bool hasSpacecraft = false;
    bool predictedImpact = false;
    bool predictedEscape = false;
    bool stableOrbit = false;
};

struct SolarMapViewState {
    bool requestedOpen = false;
    bool cameraOverrideActive = false;
    float transition = 0.0f;
    float yaw = -0.55f;
    float pitch = 0.65f;
    float zoom = 1.0f;
    bool centerOnSpacecraft = false;
    entt::entity focusedBody = entt::null;
    entt::entity cameraEntity = entt::null;
    DirectX::XMFLOAT4X4 viewProjection = {};
};

} // namespace outer_wilds::components
