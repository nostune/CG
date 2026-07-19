#pragma once

#include "../core/ECS.h"
#include "components/ObjectiveComponent.h"
#include <memory>
#include <string>

namespace outer_wilds {

class Scene;

class ObjectiveSystem final : public System {
public:
    void Initialize() override;
    void Initialize(std::shared_ptr<Scene> scene);
    void Update(float deltaTime, entt::registry& registry) override;

    static entt::entity CreateObjective(
        entt::registry& registry,
        std::string id,
        std::string title,
        std::string description = {},
        float requiredProgress = 1.0f,
        entt::entity target = entt::null);

    static void Activate(entt::registry& registry, entt::entity objective);
    static void SetProgress(entt::registry& registry, entt::entity objective, float progress);
    static void Complete(entt::registry& registry, entt::entity objective);
    static void Fail(entt::registry& registry, entt::entity objective);

private:
    static void EnsureGameState(entt::registry& registry);
    static void PublishTransition(
        entt::registry& registry,
        entt::entity entity,
        components::ObjectiveComponent& objective);

    std::weak_ptr<Scene> m_Scene;
};

} // namespace outer_wilds
