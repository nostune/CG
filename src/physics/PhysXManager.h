#pragma once
#include <PxPhysicsAPI.h>
#include <memory>

namespace outer_wilds {

class PhysXManager {
public:
    static PhysXManager& GetInstance() {
        static PhysXManager instance;
        return instance;
    }

    bool Initialize();
    void Shutdown();
    void Update(float deltaTime);
    
    physx::PxPhysics* GetPhysics() { return m_Physics; }
    physx::PxScene* GetScene() { return m_Scene; }
    physx::PxMaterial* GetDefaultMaterial() { return m_DefaultMaterial; }
    physx::PxControllerManager* GetControllerManager() { return m_ControllerManager; }

private:
    PhysXManager() = default;
    ~PhysXManager() { Shutdown(); }
    PhysXManager(const PhysXManager&) = delete;
    PhysXManager& operator=(const PhysXManager&) = delete;

    physx::PxDefaultAllocator m_Allocator;
    physx::PxDefaultErrorCallback m_ErrorCallback;
    physx::PxFoundation* m_Foundation = nullptr;
    physx::PxPhysics* m_Physics = nullptr;
    physx::PxDefaultCpuDispatcher* m_Dispatcher = nullptr;
    physx::PxScene* m_Scene = nullptr;
    physx::PxMaterial* m_DefaultMaterial = nullptr;
    physx::PxPvd* m_Pvd = nullptr;
    physx::PxControllerManager* m_ControllerManager = nullptr;
};

} // namespace outer_wilds
