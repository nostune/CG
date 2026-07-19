#include "Engine.h"
#include "DebugManager.h"
#include "TimeManager.h"
#include "../graphics/RenderSystem.h"
#include "../physics/PhysicsSystem.h"
#include "../physics/SectorPhysicsSystem.h"
#include "../physics/OrbitSystem.h"
// === 【已禁用】旧物理系统 - 等待重构 ===
// #include "../physics/GravitySystem.h"
// #include "../physics/ApplyGravitySystem.h"
// #include "../physics/SectorSystem.h"
#include "../gameplay/PlayerSystem.h"
#include "../gameplay/SpacecraftDrivingSystem.h"
#include "../gameplay/ObjectiveSystem.h"
#include "../gameplay/OrbitNavigationSystem.h"
#include "../gameplay/NavigationTargetSystem.h"
#include "../graphics/SolarMapCameraSystem.h"
// #include "../gameplay/PlayerAlignmentSystem.h"
// #include "../gameplay/OrbitSystem.h"
// #include "../gameplay/SpacecraftControlSystem.h"
// #include "../gameplay/SpacecraftKinematicSystem.h"
// === 【已禁用】旧物理系统结束 ===
#include "../gameplay/components/SpacecraftComponent.h"
#include "../graphics/FreeCameraSystem.h"
#include "../graphics/CameraModeSystem.h"
#include "../audio/AudioSystem.h"
#include "../ui/UISystem.h"
#include "../input/InputManager.h"
#include "../physics/PhysXManager.h"
#include "../scene/SceneManager.h"
#include "../graphics/resources/OBJLoader.h"
#include "../graphics/resources/TerrainGenerator.h"
#include <windows.h>
#include <iostream>

namespace outer_wilds {

bool Engine::Initialize(void* hwnd, int width, int height) {
    m_SceneManager = std::make_unique<SceneManager>();

    // Create default scene if none exists
    if (!m_SceneManager->GetActiveScene()) {
        m_SceneManager->CreateScene("default");
        m_SceneManager->SetActiveScene("default");
    }

    InputManager::GetInstance().Initialize(static_cast<HWND>(hwnd));
    DebugManager::GetInstance().Log("InputManager", "InputManager initialized");

    if (!PhysXManager::GetInstance().Initialize()) {
        DebugManager::GetInstance().Log("PhysXManager", "Failed to initialize PhysX!");
        return false;
    }

    m_RenderSystem = AddSystem<RenderSystem>();
    m_RenderSystem->Initialize(m_SceneManager.get());
    if (!m_RenderSystem->InitializeBackend(hwnd, width, height)) {
        DebugManager::GetInstance().Log("RenderSystem", "Failed to initialize RenderBackend");
        return false;
    }

    // === 物理系统 ===
    m_PhysicsSystem = AddSystem<PhysicsSystem>();
    m_PhysicsSystem->Initialize(m_SceneManager->GetActiveScene());
    
    // 扇区物理系统（重力、坐标转换）
    m_SectorPhysicsSystem = AddSystem<SectorPhysicsSystem>();
    m_SectorPhysicsSystem->Initialize(m_SceneManager->GetActiveScene());
    
    // 轨道系统（星球公转/自转）
    m_OrbitSystem = AddSystem<OrbitSystem>();
    m_OrbitSystem->Initialize(m_SceneManager->GetActiveScene());

    // === 【已禁用】旧物理/轨道/对齐系统 - 等待重构 ===
    // m_OrbitSystem = AddSystem<OrbitSystem>();
    // m_SectorSystem = AddSystem<SectorSystem>();
    // m_GravitySystem = AddSystem<GravitySystem>();
    // m_ApplyGravitySystem = AddSystem<ApplyGravitySystem>();
    // m_PlayerAlignmentSystem = AddSystem<PlayerAlignmentSystem>();
    // m_SpacecraftKinematicSystem = AddSystem<systems::SpacecraftKinematicSystem>();
    // m_SpacecraftControlSystem = AddSystem<systems::SpacecraftControlSystem>();
    // === 【已禁用】结束 ===

    m_PlayerSystem = AddSystem<PlayerSystem>();
    m_PlayerSystem->Initialize(m_SceneManager->GetActiveScene());

    m_NavigationTargetSystem = AddSystem<NavigationTargetSystem>();
    m_NavigationTargetSystem->Initialize(m_SceneManager->GetActiveScene());

    // 飞船驾驶系统（6DOF 物理控制）
    m_SpacecraftDrivingSystem = AddSystem<SpacecraftDrivingSystem>();
    m_SpacecraftDrivingSystem->Initialize(m_SceneManager->GetActiveScene());

    // 相机模式系统（处理玩家视角/自由视角切换）
    m_CameraModeSystem = AddSystem<CameraModeSystem>();
    m_CameraModeSystem->Initialize(m_SceneManager->GetActiveScene());

    // 自由相机系统（在CameraModeSystem之后，处理自由视角移动）
    m_FreeCameraSystem = AddSystem<FreeCameraSystem>();
    m_FreeCameraSystem->Initialize(m_SceneManager->GetActiveScene());

    // 音频系统
    m_AudioSystem = AddSystem<AudioSystem>();
    if (!m_AudioSystem->InitializeAudio()) {
        DebugManager::GetInstance().Log("AudioSystem", "Failed to initialize AudioSystem");
        // 不阻止游戏启动，音频系统失败不是致命错误
    } else {
        DebugManager::GetInstance().Log("AudioSystem", "AudioSystem initialized successfully");
    }

    m_ObjectiveSystem = AddSystem<ObjectiveSystem>();
    m_ObjectiveSystem->Initialize(m_SceneManager->GetActiveScene());

    m_OrbitNavigationSystem = AddSystem<OrbitNavigationSystem>();
    m_OrbitNavigationSystem->Initialize(m_SceneManager->GetActiveScene());

    m_SolarMapCameraSystem = AddSystem<SolarMapCameraSystem>();
    m_SolarMapCameraSystem->Initialize();

    // UI系统
    m_UISystem = AddSystem<UISystem>();
    auto d3d11Device = static_cast<ID3D11Device*>(m_RenderSystem->GetBackend()->GetDevice());
    auto d3d11Context = static_cast<ID3D11DeviceContext*>(m_RenderSystem->GetBackend()->GetContext());
    if (!m_UISystem->Initialize(d3d11Device, d3d11Context, static_cast<HWND>(hwnd))) {
        DebugManager::GetInstance().Log("UISystem", "Failed to initialize UISystem");
    } else {
        DebugManager::GetInstance().Log("UISystem", "UISystem initialized successfully");
    }

    m_Running = true;
    return true;
}

void Engine::Run() {
    // 启动日志（仅一次）
    // std::cout << "[Engine::Run] Starting main loop" << std::endl;
    
    while (m_Running) {
        MainLoop();
    }
}

void Engine::Shutdown() {
    m_Systems.clear();
    PhysXManager::GetInstance().Shutdown();
}

void Engine::MainLoop() {
    MSG msg = {};
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        
        if (msg.message == WM_QUIT) {
            std::cout << "[Engine] Received WM_QUIT, stopping..." << std::endl;
            m_Running = false;
        }
        if (msg.message == WM_CLOSE) {
            std::cout << "[Engine] Received WM_CLOSE" << std::endl;
        }
        if (msg.message == WM_DESTROY) {
            std::cout << "[Engine] Received WM_DESTROY" << std::endl;
        }
    }

    if (!m_Running) {
        return;
    }

    TimeManager::GetInstance().Update();
    m_DeltaTime = TimeManager::GetInstance().GetDeltaTime();

    Update();

    m_RunElapsedSeconds += m_DeltaTime;
    if (m_AutoStopAfterSeconds > 0.0f && m_RunElapsedSeconds >= m_AutoStopAfterSeconds) {
        DebugManager::GetInstance().Info("Engine", "Automatic diagnostic stop reached");
        m_Running = false;
    }
}

void Engine::Update() {
    if (!m_SceneManager || !m_SceneManager->GetActiveScene()) return;
    auto& registry = m_SceneManager->GetActiveScene()->GetRegistry();

    InputManager::GetInstance().Update();

    // The current controller, sector transform, and camera pipeline advances
    // together once per rendered frame. Fixed substeps require interpolated
    // presentation state and are intentionally not enabled yet.
    TimeManager::GetInstance().AdvanceSimulationTime(m_DeltaTime);
    if (m_OrbitSystem) m_OrbitSystem->Update(m_DeltaTime, registry);
    if (m_PlayerSystem) m_PlayerSystem->Update(m_DeltaTime, registry);
    if (m_NavigationTargetSystem) m_NavigationTargetSystem->Update(m_DeltaTime, registry);
    if (m_SpacecraftDrivingSystem) m_SpacecraftDrivingSystem->Update(m_DeltaTime, registry);
    if (m_CameraModeSystem) m_CameraModeSystem->Update(m_DeltaTime, registry);
    if (m_FreeCameraSystem) m_FreeCameraSystem->Update(m_DeltaTime, registry);
    if (m_AudioSystem) m_AudioSystem->Update(m_DeltaTime, registry);

    if (m_NavigationTargetSystem) m_NavigationTargetSystem->PrePhysicsUpdate(registry);
    if (m_OrbitNavigationSystem) m_OrbitNavigationSystem->PrePhysicsUpdate(registry);
    if (m_SpacecraftDrivingSystem) {
        m_SpacecraftDrivingSystem->PrePhysicsUpdate(m_DeltaTime, registry);
    }
    if (m_SectorPhysicsSystem) {
        m_SectorPhysicsSystem->PrePhysicsUpdate(m_DeltaTime, registry);
    }

    PhysXManager::GetInstance().Update(m_DeltaTime);

    if (m_SectorPhysicsSystem) {
        m_SectorPhysicsSystem->PostPhysicsUpdate(m_DeltaTime, registry);
    }
    if (m_SpacecraftDrivingSystem) {
        m_SpacecraftDrivingSystem->PostPhysicsStep(registry);
        m_SpacecraftDrivingSystem->PostPhysicsUpdate(m_DeltaTime, registry);
    }

    auto& diagnostics = DebugManager::GetInstance();
    diagnostics.SetMetric("Physics steps", 1.0, "steps");
    diagnostics.SetMetric("Physics step dt", static_cast<double>(m_DeltaTime) * 1000.0, "ms");
    diagnostics.SetMetric("Simulation time", TimeManager::GetInstance().GetSimulationTime(), "s");

    if (m_OrbitNavigationSystem) m_OrbitNavigationSystem->Update(m_DeltaTime, registry);
    if (m_ObjectiveSystem) m_ObjectiveSystem->Update(m_DeltaTime, registry);
    if (m_UISystem) m_UISystem->Update(m_DeltaTime, registry);
    if (m_SolarMapCameraSystem) m_SolarMapCameraSystem->Update(m_DeltaTime, registry);

    DebugManager::GetInstance().Update(m_DeltaTime);
    if (m_RenderSystem) m_RenderSystem->Update(m_DeltaTime, registry);
}

}
