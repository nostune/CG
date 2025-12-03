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

    // Create PVD (optional for debugging)
    m_Pvd = PxCreatePvd(*m_Foundation);
    physx::PxPvdTransport* transport = physx::PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
    m_Pvd->connect(*transport, physx::PxPvdInstrumentationFlag::eALL);

    // Create physics
    m_Physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_Foundation, physx::PxTolerancesScale(), true, m_Pvd);
    if (!m_Physics) {
        std::cerr << "PxCreatePhysics failed!" << std::endl;
        return false;
    }

    // Create scene
    physx::PxSceneDesc sceneDesc(m_Physics->getTolerancesScale());
    sceneDesc.gravity = physx::PxVec3(0.0f, -9.81f, 0.0f);
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
    if (m_Scene && deltaTime > 0.0f) {
        m_Scene->simulate(deltaTime);
        m_Scene->fetchResults(true);
    }
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