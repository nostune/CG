#include "Engine.h"
#include "DebugManager.h"
#include "TimeManager.h"
#include "../graphics/RenderSystem.h"
#include "../physics/PhysicsSystem.h"
#include "../physics/GravitySystem.h"
#include "../physics/ApplyGravitySystem.h"
#include "../physics/ReferenceFrameSystem.h"
#include "../gameplay/PlayerSystem.h"
#include "../gameplay/PlayerAlignmentSystem.h"
#include "../gameplay/OrbitSystem.h"
#include "../graphics/FreeCameraSystem.h"
#include "../graphics/CameraModeSystem.h"
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

    // 轨道系统（在PhysicsSystem之前，更新天体位置）
    m_OrbitSystem = AddSystem<OrbitSystem>();

    // 参考系系统（在OrbitSystem之后，补偿天体运动产生的位移）
    m_ReferenceFrameSystem = AddSystem<ReferenceFrameSystem>();

    // 重力系统（在PhysicsSystem之前，用于计算重力）
    m_GravitySystem = AddSystem<GravitySystem>();

    // 应用重力系统（在GravitySystem之后、PhysicsSystem之前，将重力力应用到刚体）
    m_ApplyGravitySystem = AddSystem<ApplyGravitySystem>();

    // 玩家对齐系统（在PlayerSystem之前，用于调整玩家姿态）
    m_PlayerAlignmentSystem = AddSystem<PlayerAlignmentSystem>();

    m_PlayerSystem = AddSystem<PlayerSystem>();
    m_PlayerSystem->Initialize(m_SceneManager->GetActiveScene());

    // 相机模式系统（处理玩家视角/自由视角切换）
    m_CameraModeSystem = AddSystem<CameraModeSystem>();
    m_CameraModeSystem->Initialize(m_SceneManager->GetActiveScene());

    // 自由相机系统（在CameraModeSystem之后，处理自由视角移动）
    m_FreeCameraSystem = AddSystem<FreeCameraSystem>();
    m_FreeCameraSystem->Initialize(m_SceneManager->GetActiveScene());

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
    if (!m_SceneManager || !m_SceneManager->GetActiveScene()) return;
    auto& registry = m_SceneManager->GetActiveScene()->GetRegistry();

    // === 更新顺序说明 ===
    // 1. OrbitSystem: 更新天体位置（圆弧运动）
    // 2. ReferenceFrameSystem: 将附着物体同步到天体位置
    // 3. GravitySystem/ApplyGravitySystem: 处理重力
    // 4. PhysX模拟: 物理碰撞、摩擦等
    // 5. ReferenceFrameSystem::PostPhysicsUpdate: 更新本地坐标
    // 6. 其他系统...

    // 执行所有系统的Update
    for (auto& system : m_Systems) {
        system->Update(m_DeltaTime, registry);
    }

    // PhysX物理模拟
    PhysXManager::GetInstance().Update(m_DeltaTime);

    // === 物理模拟后：更新参考系的本地坐标 ===
    // 这允许物理运动修改物体在天体表面的位置
    if (m_ReferenceFrameSystem) {
        m_ReferenceFrameSystem->PostPhysicsUpdate(registry);
    }

    DebugManager::GetInstance().Update(m_DeltaTime);
}

}
