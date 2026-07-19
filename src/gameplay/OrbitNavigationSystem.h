#pragma once

#include "../core/ECS.h"
#include <memory>

namespace outer_wilds {

class Scene;

class OrbitNavigationSystem final : public System {
public:
    void Initialize() override;
    void Initialize(std::shared_ptr<Scene> scene);
    void Update(float deltaTime, entt::registry& registry) override;
    void PrePhysicsUpdate(entt::registry& registry);

private:
    static void UpdateSpacecraft(entt::registry& registry, entt::entity entity);
    static void UpdateSolarMapState(entt::registry& registry);
    std::weak_ptr<Scene> m_Scene;
};

} // namespace outer_wilds
