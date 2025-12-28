#pragma once
#include "../core/ECS.h"
#include "../scene/Scene.h"
#include <DirectXMath.h>

namespace outer_wilds {

class PlayerSystem : public System {
public:
    PlayerSystem() = default;
    ~PlayerSystem() override = default;

    void Initialize() override;
    void Initialize(std::shared_ptr<Scene> scene);
    void Update(float deltaTime, entt::registry& registry) override;
    void Shutdown() override;

private:
    void ProcessPlayerInput(float deltaTime, entt::registry& registry);
    void UpdateSurfaceWalking(float deltaTime, entt::registry& registry);
    void UpdatePlayerCamera(float deltaTime, entt::registry& registry);
    void UpdateSpacecraftInteraction(float deltaTime, entt::registry& registry);
    
    // 表面行走辅助函数
    void UpdateLocalCoordinateFrame(DirectX::XMFLOAT3& localUp, DirectX::XMFLOAT3& localForward, 
                                     DirectX::XMFLOAT3& localRight, const DirectX::XMFLOAT3& gravityDir);
    bool GroundRaycast(const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& dir, 
                       float maxDist, DirectX::XMFLOAT3& hitPoint, DirectX::XMFLOAT3& hitNormal);

    entt::entity FindPlayerCamera(entt::entity playerEntity, entt::registry& registry);
    entt::entity FindCameraPlayer(entt::entity cameraEntity, entt::registry& registry);

    std::shared_ptr<Scene> m_Scene;
};

} // namespace outer_wilds
