#pragma once
#include "Scene.h"
#include "../core/ECS.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace outer_wilds {

class SceneManager : public System {
public:
    SceneManager() = default;
    ~SceneManager() override = default;

    void Initialize() override;
    void Update(float deltaTime, entt::registry& registry) override;
    void Shutdown() override;

    // Scene management
    std::shared_ptr<Scene> CreateScene(const std::string& name);
    void SetActiveScene(const std::string& name);
    std::shared_ptr<Scene> GetActiveScene();

private:
    std::unordered_map<std::string, std::shared_ptr<Scene>> m_Scenes;
    std::string m_ActiveSceneName;
};

} // namespace outer_wilds
