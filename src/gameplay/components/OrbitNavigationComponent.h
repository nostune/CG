#pragma once

#include <DirectXMath.h>
#include <entt/entt.hpp>
#include <vector>

namespace outer_wilds::components {

struct OrbitNavigationComponent {
    entt::entity centralBody = entt::null;
    entt::entity objective = entt::null;
    float bodyRadius = 0.0f;
    float influenceRadius = 0.0f;
    float altitude = 0.0f;
    float speed = 0.0f;
    float radialSpeed = 0.0f;
    float tangentialSpeed = 0.0f;
    float circularOrbitSpeed = 0.0f;
    float accumulatedAngle = 0.0f;
    float predictedPeriapsisAltitude = 0.0f;
    float predictedApoapsisAltitude = 0.0f;
    bool predictedImpact = false;
    bool predictedEscape = false;
    bool stableOrbit = false;
    bool circularizeActive = false;
    bool automatedCircularizeRequest = false;
    bool hasPreviousRadial = false;
    DirectX::XMFLOAT3 previousRadial = {1.0f, 0.0f, 0.0f};
    std::vector<DirectX::XMFLOAT3> predictedTrajectory;
};

} // namespace outer_wilds::components
