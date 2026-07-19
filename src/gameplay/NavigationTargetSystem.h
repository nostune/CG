#pragma once

#include "../core/ECS.h"
#include <memory>

namespace outer_wilds {

class Scene;

class NavigationTargetSystem : public System {
public:
    void Initialize();
    void Initialize(std::shared_ptr<Scene> scene);
    void Update(float deltaTime, entt::registry& registry) override;
    void PrePhysicsUpdate(entt::registry& registry);

private:
    std::shared_ptr<Scene> m_Scene;
    bool m_MatchWasActive = false;
};

} // namespace outer_wilds
