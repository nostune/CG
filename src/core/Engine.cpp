#include "Engine.h"
#include "DebugManager.h"
#include "TimeManager.h"
#include "../graphics/RenderSystem.h"
#include "../physics/PhysicsSystem.h"
#include "../physics/GravitySystem.h"
#include "../gameplay/PlayerSystem.h"
#include "../gameplay/PlayerAlignmentSystem.h"
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

    // 重力系统（在PhysicsSystem之前，用于计算重力）
    m_GravitySystem = AddSystem<GravitySystem>();

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
    std::cout << "[Engine::Run] IMMEDIATE: Starting main loop, m_Running=" << m_Running << std::endl;
    std::cout << "[Engine::Run] IMMEDIATE: m_Systems.size()=" << m_Systems.size() << std::endl;
    
    int loopIterations = 0;
    while (m_Running) {
        MainLoop();
        loopIterations++;
        
        // 每100次循环输出一次
        if (loopIterations % 100 == 0) {
            std::cout << "[Engine::Run] Loop iteration: " << loopIterations << std::endl;
        }
    }
    std::cout << "[Engine::Run] IMMEDIATE: Exited main loop after " << loopIterations << " iterations" << std::endl;
}

void Engine::Shutdown() {
    m_Systems.clear();
    PhysXManager::GetInstance().Shutdown();
}

void Engine::MainLoop() {
    static int loopCount = 0;
    loopCount++;
    if (loopCount <= 3) {
        std::cout << "[Engine::MainLoop] IMMEDIATE: Loop iteration " << loopCount << std::endl;
    }
    
    MSG msg = {};
    int msgCount = 0;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        msgCount++;
        if (loopCount <= 3) {
            std::cout << "[Engine::MainLoop] Message #" << msgCount << ": " << msg.message << std::endl;
        }
        
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        
        if (msg.message == WM_QUIT) {
            std::cout << "[Engine::MainLoop] IMMEDIATE: Received WM_QUIT, stopping" << std::endl;
            m_Running = false;
        }
    }

    if (!m_Running) {
        std::cout << "[Engine::MainLoop] IMMEDIATE: m_Running is FALSE, returning early" << std::endl;
        return;
    }

    TimeManager::GetInstance().Update();
    m_DeltaTime = TimeManager::GetInstance().GetDeltaTime();

    if (loopCount <= 3) {
        std::cout << "[Engine::MainLoop] About to call Update(), deltaTime=" << m_DeltaTime << std::endl;
    }

    Update();
    
    if (loopCount <= 3) {
        std::cout << "[Engine::MainLoop] Update() completed" << std::endl;
    }
}

void Engine::Update() {
    if (!m_SceneManager || !m_SceneManager->GetActiveScene()) return;
    auto& registry = m_SceneManager->GetActiveScene()->GetRegistry();

    // === DEBUG: 输出系统数量 ===
    static int updateCount = 0;
    if (updateCount++ < 3) {
        std::cout << "[Engine::Update] IMMEDIATE: Called #" << updateCount 
                  << ", systems count: " << m_Systems.size() << std::endl;
    }

    // Update PhysX simulation
    PhysXManager::GetInstance().Update(m_DeltaTime);

    for (auto& system : m_Systems) {
        system->Update(m_DeltaTime, registry);
    }

    DebugManager::GetInstance().Update(m_DeltaTime);
}

}
