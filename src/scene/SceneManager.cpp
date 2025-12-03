#include "SceneManager.h"

namespace outer_wilds {

void SceneManager::Initialize() {
    // Create default scene if none exists
    if (m_Scenes.empty()) {
        CreateScene("Default");
        SetActiveScene("Default");
    }
}

void SceneManager::Update(float deltaTime, entt::registry& registry) {
    // Scene update logic (if needed)
}

void SceneManager::Shutdown() {
    m_Scenes.clear();
    m_ActiveSceneName.clear();
}

std::shared_ptr<Scene> SceneManager::CreateScene(const std::string& name) {
    auto scene = std::make_shared<Scene>(name);
    m_Scenes[name] = scene;
    return scene;
}

void SceneManager::SetActiveScene(const std::string& name) {
    if (m_Scenes.find(name) != m_Scenes.end()) {
        m_ActiveSceneName = name;
    }
}

std::shared_ptr<Scene> SceneManager::GetActiveScene() {
    if (m_ActiveSceneName.empty() || m_Scenes.find(m_ActiveSceneName) == m_Scenes.end()) {
        return nullptr;
    }
    return m_Scenes[m_ActiveSceneName];
}

} // namespace outer_wilds
