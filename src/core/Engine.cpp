#include "Engine.h"
#include "DebugManager.h"
#include "TimeManager.h"
#include "../graphics/RenderSystem.h"
#include "../physics/PhysicsSystem.h"
#include "../physics/GravitySystem.h"
#include "../physics/ApplyGravitySystem.h"
#include "../physics/SectorSystem.h"
#include "../gameplay/PlayerSystem.h"
#include "../gameplay/PlayerAlignmentSystem.h"
#include "../gameplay/OrbitSystem.h"
#include "../gameplay/SpacecraftControlSystem.h"
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

    m_PhysicsSystem = AddSystem<PhysicsSystem>();
    m_PhysicsSystem->Initialize(m_SceneManager->GetActiveScene());

    // 轨道系统（更新天体世界位置）
    m_OrbitSystem = AddSystem<OrbitSystem>();

    // Sector系统（管理局部坐标系，在轨道更新后、物理模拟前）
    m_SectorSystem = AddSystem<SectorSystem>();

    // 重力系统（在PhysicsSystem之前，用于计算重力）
    m_GravitySystem = AddSystem<GravitySystem>();

    // 应用重力系统（在GravitySystem之后、PhysicsSystem之前，将重力力应用到刚体）
    m_ApplyGravitySystem = AddSystem<ApplyGravitySystem>();

    // 玩家对齐系统（在PlayerSystem之前，用于调整玩家姿态）
    m_PlayerAlignmentSystem = AddSystem<PlayerAlignmentSystem>();

    m_PlayerSystem = AddSystem<PlayerSystem>();
    m_PlayerSystem->Initialize(m_SceneManager->GetActiveScene());

    // 飞船控制系统（纯物理模拟）
    m_SpacecraftControlSystem = AddSystem<systems::SpacecraftControlSystem>();

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
            m_Running = false;
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
    static int frameNum = 0;
    frameNum++;
    
    if (frameNum <= 3) {
        std::cout << "[Engine::Update] Frame " << frameNum << " start" << std::endl;
    }
    
    if (!m_SceneManager || !m_SceneManager->GetActiveScene()) return;
    auto& registry = m_SceneManager->GetActiveScene()->GetRegistry();

    // === 更新顺序说明 ===
    // 1. OrbitSystem: 更新天体位置（圆弧运动）
    // 2. SectorSystem::Update: 同步Sector世界位置
    // 3. GravitySystem/ApplyGravitySystem: 处理重力
    // 4. PlayerSystem/CameraModeSystem/FreeCameraSystem: 处理玩家输入和相机
    // 5. PhysX模拟: 物理碰撞、摩擦等
    // 6. SectorSystem::PostPhysicsUpdate: 将局部坐标转换为世界坐标
    // 7. RenderSystem: 渲染（最后执行！）

    // 执行所有系统的Update（跳过RenderSystem，最后单独调用）
    int sysIdx = 0;
    for (auto& system : m_Systems) {
        // 跳过 RenderSystem，它需要在最后执行
        if (system.get() == static_cast<System*>(m_RenderSystem.get())) {
            continue;
        }
        if (frameNum <= 3) {
            std::cout << "[Engine::Update] Frame " << frameNum << " system " << sysIdx << " start" << std::endl;
        }
        system->Update(m_DeltaTime, registry);
        if (frameNum <= 3) {
            std::cout << "[Engine::Update] Frame " << frameNum << " system " << sysIdx << " done" << std::endl;
        }
        sysIdx++;
    }
    
    if (frameNum <= 300) {
        std::cout << "[Engine::Update] Frame " << frameNum << " PhysX start" << std::endl;
    }
    // PhysX物理模拟
    PhysXManager::GetInstance().Update(m_DeltaTime);

    // Sector系统后处理：同步PhysX结果到世界坐标（在渲染前！）
    if (m_SectorSystem) {
        m_SectorSystem->PostPhysicsUpdate(registry);
    }

    DebugManager::GetInstance().Update(m_DeltaTime);

    // 渲染系统最后执行（在所有坐标转换完成后）
    if (m_RenderSystem) {
        m_RenderSystem->Update(m_DeltaTime, registry);
    }
}

}
