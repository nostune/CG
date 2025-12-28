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
    // 【修复】禁用全局重力，使用自定义星球重力系统 (GravitySystem + ApplyGravitySystem)
    DirectX::XMFLOAT3 m_Gravity = { 0.0f, 0.0f, 0.0f };
    float m_TimeAccumulator = 0.0f;
    float m_FixedTimeStep = 1.0f / 60.0f;
};

} // namespace outer_wilds
