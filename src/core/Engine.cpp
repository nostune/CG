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
    DebugManager::GetInstance().Log("PhysXManager", "PhysX initialized successfully!");

    m_RenderSystem = AddSystem<RenderSystem>();
    m_RenderSystem->Initialize(m_SceneManager.get());
    if (!m_RenderSystem->InitializeBackend(hwnd, width, height)) {
        DebugManager::GetInstance().Log("RenderSystem", "Failed to initialize RenderBackend");
        return false;
    }
    DebugManager::GetInstance().Log("RenderBackend", "RenderBackend initialized successfully");

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
}

void Engine::Update() {
    if (!m_SceneManager || !m_SceneManager->GetActiveScene()) return;
    auto& registry = m_SceneManager->GetActiveScene()->GetRegistry();

    // ============================================
    // 更新顺序（严格按此执行）
    // ============================================
    // 1. 轨道系统（更新星球位置）
    // 2. 输入/游戏逻辑系统
    // 3. SectorPhysicsSystem::PrePhysicsUpdate (重力、力)
    // 4. PhysXManager::Update (simulate + fetchResults)
    // 5. SectorPhysicsSystem::PostPhysicsUpdate (坐标转换、扇区同步)
    // 6. 相机系统
    // 7. 渲染系统
    // 
    // 【规则】任何修改 PhysX Actor 的代码必须标注:
    //   // [来源: XXXSystem] 操作描述
    // ============================================

    // 1. 轨道系统：更新星球公转/自转位置（必须在其他系统之前）
    if (m_OrbitSystem) {
        m_OrbitSystem->Update(m_DeltaTime, registry);
    }
    
    // 2. 游戏逻辑系统（跳过 RenderSystem、SectorPhysicsSystem、OrbitSystem）
    for (auto& system : m_Systems) {
        if (system.get() == static_cast<System*>(m_RenderSystem.get())) continue;
        if (system.get() == static_cast<System*>(m_SectorPhysicsSystem.get())) continue;
        if (system.get() == static_cast<System*>(m_OrbitSystem.get())) continue;
        system->Update(m_DeltaTime, registry);
    }
    
    // 3. 物理前处理：计算重力、应用力、同步 Kinematic
    if (m_SectorPhysicsSystem) {
        m_SectorPhysicsSystem->PrePhysicsUpdate(m_DeltaTime, registry);
    }
    
    // 4. PhysX 物理模拟
    PhysXManager::GetInstance().Update(m_DeltaTime);
    
    // 5. 物理后处理：读取结果、坐标转换、扇区同步
    if (m_SectorPhysicsSystem) {
        m_SectorPhysicsSystem->PostPhysicsUpdate(m_DeltaTime, registry);
    }

    DebugManager::GetInstance().Update(m_DeltaTime);

    // 6-7. 渲染系统最后执行
    if (m_RenderSystem) {
        m_RenderSystem->Update(m_DeltaTime, registry);
    }
}

}
