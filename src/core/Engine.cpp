#include "Engine.h"
#include "DebugManager.h"
#include "TimeManager.h"
#include "../graphics/RenderSystem.h"
#include "../physics/PhysicsSystem.h"
#include "../gameplay/PlayerSystem.h"
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

    m_PlayerSystem = AddSystem<PlayerSystem>();
    m_PlayerSystem->Initialize(m_SceneManager->GetActiveScene());

    m_Running = true;
    return true;
}

void Engine::Run() {
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

    if (!m_Running) return;

    TimeManager::GetInstance().Update();
    m_DeltaTime = TimeManager::GetInstance().GetDeltaTime();

    Update();
}

void Engine::Update() {
    if (!m_SceneManager || !m_SceneManager->GetActiveScene()) return;
    auto& registry = m_SceneManager->GetActiveScene()->GetRegistry();

    // Update PhysX simulation
    PhysXManager::GetInstance().Update(m_DeltaTime);

    for (auto& system : m_Systems) {
        system->Update(m_DeltaTime, registry);
    }

    DebugManager::GetInstance().Update(m_DeltaTime);
}

}
