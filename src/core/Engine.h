#pragma once
#include "ECS.h"
#include "../scene/SceneManager.h"
#include "../graphics/resources/Mesh.h"
#include <memory>
#include <vector>

namespace outer_wilds {

// 核心系统前向声明
class RenderSystem;
class PhysicsSystem;
class SectorPhysicsSystem;
class OrbitSystem;
class PlayerSystem;
class SpacecraftDrivingSystem;
class FreeCameraSystem;
class CameraModeSystem;
class AudioSystem;
class UISystem;

class Engine {
public:
    static Engine& GetInstance() {
        static Engine instance;
        return instance;
    }

    bool Initialize(void* hwnd, int width, int height);
    void Run();
    void Shutdown();
    void Stop() { m_Running = false; }

    template<typename T, typename... Args>
    std::shared_ptr<T> AddSystem(Args&&... args) {
        auto system = std::make_shared<T>(std::forward<Args>(args)...);
        m_Systems.push_back(system);
        return system;
    }

    SceneManager* GetSceneManager() { return m_SceneManager.get(); }
    RenderSystem* GetRenderSystem() { return m_RenderSystem.get(); }
    CameraModeSystem* GetCameraModeSystem() { return m_CameraModeSystem.get(); }
    AudioSystem* GetAudioSystem() { return m_AudioSystem.get(); }
    UISystem* GetUISystem() { return m_UISystem.get(); }

private:
    Engine() = default;
    ~Engine() = default;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void MainLoop();
    void Update();

    bool m_Running = false;
    float m_DeltaTime = 0.0f;

    std::unique_ptr<SceneManager> m_SceneManager;
    std::vector<std::shared_ptr<System>> m_Systems;
    
    // Core systems
    std::shared_ptr<RenderSystem> m_RenderSystem;
    std::shared_ptr<PhysicsSystem> m_PhysicsSystem;
    std::shared_ptr<SectorPhysicsSystem> m_SectorPhysicsSystem;
    std::shared_ptr<OrbitSystem> m_OrbitSystem;
    std::shared_ptr<PlayerSystem> m_PlayerSystem;
    std::shared_ptr<SpacecraftDrivingSystem> m_SpacecraftDrivingSystem;
    std::shared_ptr<FreeCameraSystem> m_FreeCameraSystem;
    std::shared_ptr<CameraModeSystem> m_CameraModeSystem;
    std::shared_ptr<AudioSystem> m_AudioSystem;
    std::shared_ptr<UISystem> m_UISystem;

    // Common resources
    std::shared_ptr<resources::Mesh> m_PlanetMesh;
};

}
