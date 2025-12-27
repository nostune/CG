#pragma once
#include "../core/ECS.h"
#include "../scene/Scene.h"

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
    void UpdatePlayerMovement(float deltaTime, entt::registry& registry);
    void UpdatePlayerCamera(float deltaTime, entt::registry& registry);
    void UpdateSpacecraftInteraction(float deltaTime, entt::registry& registry);

    entt::entity FindPlayerCamera(entt::entity playerEntity, entt::registry& registry);
    entt::entity FindCameraPlayer(entt::entity cameraEntity, entt::registry& registry);

    std::shared_ptr<Scene> m_Scene;
};

} // namespace outer_wilds
