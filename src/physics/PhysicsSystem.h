#pragma once
#include "../core/ECS.h"
#include "../scene/Scene.h"
#include <DirectXMath.h>

namespace outer_wilds {

class PhysXManager;

class PhysicsSystem : public System {
public:
    PhysicsSystem() = default;
    ~PhysicsSystem() override = default;

    void Initialize() override;
    void Initialize(std::shared_ptr<Scene> scene);
    void Update(float deltaTime, entt::registry& registry) override;
    void Shutdown() override;
    
    void SetGravity(const DirectX::XMFLOAT3& gravity);
    DirectX::XMFLOAT3 GetGravity() const { return m_Gravity; }

private:
    void SyncPhysicsToTransforms(entt::registry& registry);
    void SyncTransformsToPhysics(entt::registry& registry);

    std::shared_ptr<Scene> m_Scene;
    DirectX::XMFLOAT3 m_Gravity = { 0.0f, -9.81f, 0.0f };
    float m_TimeAccumulator = 0.0f;
    float m_FixedTimeStep = 1.0f / 60.0f;
};

} // namespace outer_wilds
