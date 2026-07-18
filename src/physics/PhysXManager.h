#pragma once
#include <PxPhysicsAPI.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace outer_wilds {

// Custom error callback to capture PhysX errors
class CustomErrorCallback : public physx::PxErrorCallback {
public:
    void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line) override;
};

struct ContactFrameStats {
    physx::PxU32 pairCount = 0;
    physx::PxU32 pointCount = 0;
    float maxImpulse = 0.0f;
    float minSeparation = 0.0f;
    physx::PxU32 wakeCount = 0;
    physx::PxU32 sleepCount = 0;
};

struct ActorContactState {
    physx::PxU32 pointCount = 0;
    float maxImpulse = 0.0f;
    float minSeparation = 0.0f;
    physx::PxVec3 position = physx::PxVec3(0.0f);
    physx::PxVec3 normal = physx::PxVec3(0.0f);
};

class ContactReportCallback : public physx::PxSimulationEventCallback {
public:
    void onConstraintBreak(physx::PxConstraintInfo*, physx::PxU32) override {}
    void onWake(physx::PxActor** actors, physx::PxU32 count) override;
    void onSleep(physx::PxActor** actors, physx::PxU32 count) override;
    void onTrigger(physx::PxTriggerPair*, physx::PxU32) override {}
    void onAdvance(
        const physx::PxRigidBody* const*, const physx::PxTransform*, physx::PxU32) override {}
    void onContact(
        const physx::PxContactPairHeader& pairHeader,
        const physx::PxContactPair* pairs,
        physx::PxU32 pairCount) override;

    ContactFrameStats ConsumeFrameStats();
    void BeginFrame();
    bool GetActorContactState(const physx::PxActor* actor, ActorContactState& state) const;
    void SetDetailedLoggingEnabled(bool enabled) { m_DetailedLoggingEnabled.store(enabled); }
    bool IsDetailedLoggingEnabled() const { return m_DetailedLoggingEnabled.load(); }

private:
    mutable std::mutex m_Mutex;
    ContactFrameStats m_FrameStats;
    std::unordered_map<const physx::PxActor*, ActorContactState> m_ActorContacts;
    std::atomic_bool m_DetailedLoggingEnabled = false;
    std::chrono::steady_clock::time_point m_LastDetailedLog;
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

    bool ConnectPvd(const char* host = "127.0.0.1", int port = 5425, unsigned int timeoutMs = 100);
    void DisconnectPvd();
    bool IsPvdConnected() const;
    const std::string& GetPvdStatus() const { return m_PvdStatus; }

    void SetDebugVisualizationEnabled(bool enabled);
    bool IsDebugVisualizationEnabled() const { return m_DebugVisualizationEnabled; }
    physx::PxU32 GetDebugLineCount() const;
    physx::PxU32 GetDebugTriangleCount() const;
    physx::PxU32 GetRigidActorCount() const;
    const physx::PxRenderBuffer* GetDebugRenderBuffer() const;
    void SetContactLoggingEnabled(bool enabled) { m_ContactReportCallback.SetDetailedLoggingEnabled(enabled); }
    bool IsContactLoggingEnabled() const { return m_ContactReportCallback.IsDetailedLoggingEnabled(); }
    bool GetActorContactState(const physx::PxActor* actor, ActorContactState& state) const {
        return m_ContactReportCallback.GetActorContactState(actor, state);
    }
    
    // 崩溃时转储场景状态
    void DumpSceneState(const char* context);
    
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
    ContactReportCallback m_ContactReportCallback;
    physx::PxFoundation* m_Foundation = nullptr;
    physx::PxPhysics* m_Physics = nullptr;
    physx::PxDefaultCpuDispatcher* m_Dispatcher = nullptr;
    physx::PxScene* m_Scene = nullptr;
    physx::PxMaterial* m_DefaultMaterial = nullptr;
    physx::PxPvd* m_Pvd = nullptr;
    physx::PxPvdTransport* m_PvdTransport = nullptr;
    physx::PxControllerManager* m_ControllerManager = nullptr;
    std::string m_PvdStatus = "Not initialized";
    bool m_DebugVisualizationEnabled = false;
    bool m_DebugGeometryReported = false;
};

} // namespace outer_wilds
