/**
 * OrbitSystem.h
 * 
 * 轨道系统 - 计算天体的公转和自转
 * 
 * 职责：
 * - 更新每个有 OrbitComponent 的实体的轨道位置
 * - 更新 SectorComponent.worldPosition（扇区世界坐标）
 * - 更新 TransformComponent（渲染坐标）
 */

#pragma once
#include "../core/ECS.h"
#include "../scene/Scene.h"
#include <DirectXMath.h>
#include <memory>

namespace outer_wilds {

class OrbitSystem : public System {
public:
    OrbitSystem() = default;
    ~OrbitSystem() override = default;

    void Initialize() override;
    void Initialize(std::shared_ptr<Scene> scene);
    void Update(float deltaTime, entt::registry& registry) override;
    void Shutdown() override;

private:
    // 计算轨道位置
    void UpdateOrbits(float deltaTime, entt::registry& registry);
    
    // 计算自转
    void UpdateRotations(float deltaTime, entt::registry& registry);
    
    // 计算轨道上的位置
    DirectX::XMFLOAT3 CalculateOrbitPosition(
        const DirectX::XMFLOAT3& center,
        float radius,
        float angle,
        const DirectX::XMFLOAT3& normal,
        float inclination
    );

    std::shared_ptr<Scene> m_Scene;
};

} // namespace outer_wilds
