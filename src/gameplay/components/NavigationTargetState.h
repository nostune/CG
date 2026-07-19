#pragma once

#include <DirectXMath.h>
#include <entt/entt.hpp>
#include <string>

namespace outer_wilds::components {

struct NavigationTargetState {
    entt::entity candidateTarget = entt::null;
    entt::entity lockedTarget = entt::null;
    std::string targetName;
    DirectX::XMFLOAT3 candidateWorldPosition = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 targetWorldPosition = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 targetVelocityLocal = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 relativeVelocityLocal = {0.0f, 0.0f, 0.0f};
    float targetRadius = 1.0f;
    float distance = 0.0f;
    float relativeSpeed = 0.0f;
    float matchAcceleration = 15.0f;
    float matchResponse = 6.0f;
    bool hasCandidate = false;
    bool matchingVelocity = false;
    bool automatedMatchRequest = false;
    bool matchCompletionLogged = false;
};

} // namespace outer_wilds::components
