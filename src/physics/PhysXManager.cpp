/**
 * PhysXManager.cpp
 * 
 * PhysX 物理引擎封装
 * 
 * 【重要】这是唯一可以调用 PhysX simulate/fetchResults 的地方
 * 任何系统要修改 PhysX Actor 状态，必须：
 *   1. 在 simulate() 之前完成
 *   2. 在代码中声明 "// [来源: XXXSystem]"
 */

#include "PhysXManager.h"
#include "../core/DebugManager.h"
#include <iostream>

namespace outer_wilds {

bool PhysXManager::Initialize() {
    // Create foundation
    m_Foundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_Allocator, m_ErrorCallback);
    if (!m_Foundation) {
        std::cerr << "PxCreateFoundation failed!" << std::endl;
        return false;
    }

    // Create PVD (Visual Debugger, optional)
    m_Pvd = PxCreatePvd(*m_Foundation);
    physx::PxPvdTransport* transport = physx::PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
    m_Pvd->connect(*transport, physx::PxPvdInstrumentationFlag::eALL);

    // Create physics with tolerances for large objects (planet ~64m)
    physx::PxTolerancesScale tolerances;
    tolerances.length = 10.0f;
    tolerances.speed = 100.0f;
    
    m_Physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_Foundation, tolerances, true, m_Pvd);
    if (!m_Physics) {
        std::cerr << "PxCreatePhysics failed!" << std::endl;
        return false;
    }

    // Create scene
    physx::PxSceneDesc sceneDesc(m_Physics->getTolerancesScale());
    sceneDesc.gravity = physx::PxVec3(0.0f, 0.0f, 0.0f);  // 全局重力由 GravitySystem 控制
    
    m_Dispatcher = physx::PxDefaultCpuDispatcherCreate(2);
    sceneDesc.cpuDispatcher = m_Dispatcher;
    sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;
    
    m_Scene = m_Physics->createScene(sceneDesc);
    if (!m_Scene) {
        std::cerr << "createScene failed!" << std::endl;
        return false;
    }

    // Create default material
    m_DefaultMaterial = m_Physics->createMaterial(0.5f, 0.5f, 0.6f);

    // Create character controller manager
    m_ControllerManager = PxCreateControllerManager(*m_Scene);
    if (!m_ControllerManager) {
        std::cerr << "PxCreateControllerManager failed!" << std::endl;
        return false;
    }

    DebugManager::GetInstance().Log("PhysXManager", "PhysX initialized successfully!");
    return true;
}

void PhysXManager::Update(float deltaTime) {
    if (!m_Scene || deltaTime <= 0.0f) return;
    
    // 固定时间步
    const float fixedStep = 1.0f / 60.0f;
    const int maxSubSteps = 4;
    float dt = (std::min)(deltaTime, fixedStep * maxSubSteps);
    
    // 模拟
    m_Scene->simulate(dt);
    m_Scene->fetchResults(true);
}

void PhysXManager::Shutdown() {
    if (m_ControllerManager) {
        m_ControllerManager->release();
        m_ControllerManager = nullptr;
    }

    if (m_Scene) {
        m_Scene->release();
        m_Scene = nullptr;
    }

    if (m_Dispatcher) {
        m_Dispatcher->release();
        m_Dispatcher = nullptr;
    }

    if (m_DefaultMaterial) {
        m_DefaultMaterial->release();
        m_DefaultMaterial = nullptr;
    }

    if (m_Physics) {
        m_Physics->release();
        m_Physics = nullptr;
    }

    if (m_Pvd) {
        m_Pvd->release();
        m_Pvd = nullptr;
    }

    if (m_Foundation) {
        m_Foundation->release();
        m_Foundation = nullptr;
    }

    DebugManager::GetInstance().Log("PhysXManager", "PhysX shutdown successfully!");
}

} // namespace outer_wilds
