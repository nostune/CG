#pragma once
#include <PxPhysicsAPI.h>
#include <memory>
#include <iostream>

namespace outer_wilds {

// Custom error callback to capture PhysX errors
class CustomErrorCallback : public physx::PxErrorCallback {
public:
    virtual void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line) override {
        const char* errorName = "UNKNOWN";
        switch(code) {
            case physx::PxErrorCode::eNO_ERROR: errorName = "NO_ERROR"; break;
            case physx::PxErrorCode::eDEBUG_INFO: errorName = "DEBUG_INFO"; break;
            case physx::PxErrorCode::eDEBUG_WARNING: errorName = "DEBUG_WARNING"; break;
            case physx::PxErrorCode::eINVALID_PARAMETER: errorName = "INVALID_PARAMETER"; break;
            case physx::PxErrorCode::eINVALID_OPERATION: errorName = "INVALID_OPERATION"; break;
            case physx::PxErrorCode::eOUT_OF_MEMORY: errorName = "OUT_OF_MEMORY"; break;
            case physx::PxErrorCode::eINTERNAL_ERROR: errorName = "INTERNAL_ERROR"; break;
            case physx::PxErrorCode::eABORT: errorName = "ABORT"; break;
            case physx::PxErrorCode::ePERF_WARNING: errorName = "PERF_WARNING"; break;
            case physx::PxErrorCode::eMASK_ALL: errorName = "MASK_ALL"; break;
        }
        std::cout << "[PhysX " << errorName << "] " << message << " (" << file << ":" << line << ")" << std::endl;
        std::cout.flush();
    }
};

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
    CustomErrorCallback m_ErrorCallback;
    physx::PxFoundation* m_Foundation = nullptr;
    physx::PxPhysics* m_Physics = nullptr;
    physx::PxDefaultCpuDispatcher* m_Dispatcher = nullptr;
    physx::PxScene* m_Scene = nullptr;
    physx::PxMaterial* m_DefaultMaterial = nullptr;
    physx::PxPvd* m_Pvd = nullptr;
    physx::PxControllerManager* m_ControllerManager = nullptr;
};

} // namespace outer_wilds
