#pragma once

#include "../core/ECS.h"
#include <DirectXMath.h>

namespace outer_wilds {

class SolarMapCameraSystem final : public System {
public:
    void Initialize() override;
    void Update(float deltaTime, entt::registry& registry) override;

private:
    DirectX::XMFLOAT3 m_DisplayedPosition = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 m_DisplayedTarget = {0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT3 m_DisplayedUp = {0.0f, 1.0f, 0.0f};
    float m_DisplayedFov = 75.0f;
    bool m_WasRequestedOpen = false;
    bool m_HasDisplayedPose = false;
    entt::entity m_MapCameraEntity = entt::null;
};

} // namespace outer_wilds
