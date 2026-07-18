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

#include <cstdlib>
#include <algorithm>
#include <sstream>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace outer_wilds {

namespace {

bool IsEnvironmentEnabled(const char* name) {
#ifdef _WIN32
    char value[16] = {};
    const DWORD length = GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value)));
    return length > 0 && length < sizeof(value) && std::string(value) != "0";
#else
    const char* value = std::getenv(name);
    return value != nullptr && std::string(value) != "0";
#endif
}

const char* PhysXErrorName(physx::PxErrorCode::Enum code) {
    switch (code) {
        case physx::PxErrorCode::eNO_ERROR: return "NO_ERROR";
        case physx::PxErrorCode::eDEBUG_INFO: return "DEBUG_INFO";
        case physx::PxErrorCode::eDEBUG_WARNING: return "DEBUG_WARNING";
        case physx::PxErrorCode::eINVALID_PARAMETER: return "INVALID_PARAMETER";
        case physx::PxErrorCode::eINVALID_OPERATION: return "INVALID_OPERATION";
        case physx::PxErrorCode::eOUT_OF_MEMORY: return "OUT_OF_MEMORY";
        case physx::PxErrorCode::eINTERNAL_ERROR: return "INTERNAL_ERROR";
        case physx::PxErrorCode::eABORT: return "ABORT";
        case physx::PxErrorCode::ePERF_WARNING: return "PERF_WARNING";
        case physx::PxErrorCode::eMASK_ALL: return "MASK_ALL";
    }
    return "UNKNOWN";
}

physx::PxFilterFlags ContactReportFilterShader(
    physx::PxFilterObjectAttributes attributes0,
    physx::PxFilterData,
    physx::PxFilterObjectAttributes attributes1,
    physx::PxFilterData,
    physx::PxPairFlags& pairFlags,
    const void*,
    physx::PxU32) {
    if (physx::PxFilterObjectIsTrigger(attributes0) ||
        physx::PxFilterObjectIsTrigger(attributes1)) {
        pairFlags = physx::PxPairFlag::eTRIGGER_DEFAULT;
    } else {
        pairFlags = physx::PxPairFlag::eCONTACT_DEFAULT |
                    physx::PxPairFlag::eNOTIFY_TOUCH_FOUND |
                    physx::PxPairFlag::eNOTIFY_TOUCH_PERSISTS |
                    physx::PxPairFlag::eNOTIFY_TOUCH_LOST |
                    physx::PxPairFlag::eNOTIFY_CONTACT_POINTS;
    }
    return physx::PxFilterFlag::eDEFAULT;
}

} // namespace

void ContactReportCallback::onContact(
    const physx::PxContactPairHeader& pairHeader,
    const physx::PxContactPair* pairs,
    physx::PxU32 pairCount) {
    ContactFrameStats update;
    update.pairCount = pairCount;
    physx::PxVec3 samplePosition(0.0f);
    physx::PxVec3 sampleNormal(0.0f);
    bool hasSamplePoint = false;

    for (physx::PxU32 pairIndex = 0; pairIndex < pairCount; ++pairIndex) {
        physx::PxContactPairPoint points[32];
        const physx::PxU32 pointCount = pairs[pairIndex].extractContacts(points, 32);
        update.pointCount += pointCount;
        for (physx::PxU32 pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
            if (!hasSamplePoint) {
                samplePosition = points[pointIndex].position;
                sampleNormal = points[pointIndex].normal;
                hasSamplePoint = true;
            }
            update.maxImpulse = (std::max)(update.maxImpulse, points[pointIndex].impulse.magnitude());
            update.minSeparation = (std::min)(update.minSeparation, points[pointIndex].separation);
        }

        if (pointCount > 0) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            for (physx::PxU32 actorIndex = 0; actorIndex < 2; ++actorIndex) {
                const physx::PxActor* actor = pairHeader.actors[actorIndex];
                if (!actor) continue;

                auto& state = m_ActorContacts[actor];
                const bool firstPoints = state.pointCount == 0;
                state.pointCount += pointCount;
                for (physx::PxU32 pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
                    const auto& point = points[pointIndex];
                    const float impulse = point.impulse.magnitude();
                    if (firstPoints || impulse >= state.maxImpulse) {
                        state.position = point.position;
                        state.normal = actorIndex == 0 ? point.normal : -point.normal;
                    }
                    state.maxImpulse = (std::max)(state.maxImpulse, impulse);
                    state.minSeparation = (std::min)(state.minSeparation, point.separation);
                }
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_FrameStats.pairCount += update.pairCount;
        m_FrameStats.pointCount += update.pointCount;
        m_FrameStats.maxImpulse = (std::max)(m_FrameStats.maxImpulse, update.maxImpulse);
        m_FrameStats.minSeparation = (std::min)(m_FrameStats.minSeparation, update.minSeparation);
    }

    if (!m_DetailedLoggingEnabled || update.pointCount == 0) return;

    const auto now = std::chrono::steady_clock::now();
    if (m_LastDetailedLog.time_since_epoch().count() > 0 &&
        now - m_LastDetailedLog < std::chrono::milliseconds(250)) {
        return;
    }
    m_LastDetailedLog = now;

    const char* actor0 = pairHeader.actors[0] && pairHeader.actors[0]->getName()
        ? pairHeader.actors[0]->getName() : "Unnamed actor";
    const char* actor1 = pairHeader.actors[1] && pairHeader.actors[1]->getName()
        ? pairHeader.actors[1]->getName() : "Unnamed actor";
    std::ostringstream sample;
    sample << actor0 << " <-> " << actor1
           << ": points=" << update.pointCount
           << ", maxImpulse=" << update.maxImpulse
           << ", minSeparation=" << update.minSeparation;
    if (hasSamplePoint) {
        sample << ", position=(" << samplePosition.x << ',' << samplePosition.y << ',' << samplePosition.z << ')'
               << ", normal=(" << sampleNormal.x << ',' << sampleNormal.y << ',' << sampleNormal.z << ')';
    }
    DebugManager::GetInstance().Info("PhysXContact", sample.str());
}

void ContactReportCallback::BeginFrame() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_ActorContacts.clear();
}

bool ContactReportCallback::GetActorContactState(
    const physx::PxActor* actor, ActorContactState& state) const {
    if (!actor) return false;
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto it = m_ActorContacts.find(actor);
    if (it == m_ActorContacts.end()) return false;
    state = it->second;
    return true;
}

ContactFrameStats ContactReportCallback::ConsumeFrameStats() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const ContactFrameStats result = m_FrameStats;
    m_FrameStats = {};
    return result;
}

void ContactReportCallback::onWake(physx::PxActor** actors, physx::PxU32 count) {
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_FrameStats.wakeCount += count;
    }
    if (m_DetailedLoggingEnabled.load() && count > 0) {
        const char* name = actors[0] && actors[0]->getName() ? actors[0]->getName() : "Unnamed actor";
        DebugManager::GetInstance().Info("PhysXState", std::string(name) + " woke");
    }
}

void ContactReportCallback::onSleep(physx::PxActor** actors, physx::PxU32 count) {
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_FrameStats.sleepCount += count;
    }
    if (m_DetailedLoggingEnabled.load() && count > 0) {
        const char* name = actors[0] && actors[0]->getName() ? actors[0]->getName() : "Unnamed actor";
        DebugManager::GetInstance().Info("PhysXState", std::string(name) + " slept");
    }
}

void CustomErrorCallback::reportError(
    physx::PxErrorCode::Enum code,
    const char* message,
    const char* file,
    int line) {
    std::ostringstream details;
    details << PhysXErrorName(code) << ": " << (message ? message : "")
            << " (" << (file ? file : "unknown") << ':' << line << ')';

    const bool warning = code == physx::PxErrorCode::eDEBUG_WARNING ||
                         code == physx::PxErrorCode::ePERF_WARNING;
    if (warning) {
        DebugManager::GetInstance().Warning("PhysX", details.str());
    } else if (code == physx::PxErrorCode::eDEBUG_INFO || code == physx::PxErrorCode::eNO_ERROR) {
        DebugManager::GetInstance().Info("PhysX", details.str());
    } else {
        DebugManager::GetInstance().Error("PhysX", details.str());
    }
}

bool PhysXManager::Initialize() {
    // Create foundation
    m_Foundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_Allocator, m_ErrorCallback);
    if (!m_Foundation) {
        DebugManager::GetInstance().Critical("PhysX", "PxCreateFoundation failed");
        return false;
    }

    // PVD is optional and can connect before startup or later from Diagnostics.
    m_Pvd = PxCreatePvd(*m_Foundation);
    m_PvdStatus = m_Pvd ? "Ready, disconnected" : "PxCreatePvd failed";
    if (m_Pvd && IsEnvironmentEnabled("OUTERWILDS_PVD")) {
        ConnectPvd();
    }

    // Create physics with tolerances for large objects (planet ~64m)
    physx::PxTolerancesScale tolerances;
    tolerances.length = 1.0f;
    tolerances.speed = 10.0f;
    
    m_Physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_Foundation, tolerances, true, m_Pvd);
    if (!m_Physics) {
        DebugManager::GetInstance().Critical("PhysX", "PxCreatePhysics failed");
        return false;
    }

    // Create scene
    physx::PxSceneDesc sceneDesc(m_Physics->getTolerancesScale());
    sceneDesc.gravity = physx::PxVec3(0.0f, 0.0f, 0.0f);  // 全局重力由 GravitySystem 控制
    
    m_Dispatcher = physx::PxDefaultCpuDispatcherCreate(2);
    sceneDesc.cpuDispatcher = m_Dispatcher;
    sceneDesc.filterShader = ContactReportFilterShader;
    sceneDesc.simulationEventCallback = &m_ContactReportCallback;
    sceneDesc.flags |= physx::PxSceneFlag::eENABLE_CCD;
    m_ContactReportCallback.SetDetailedLoggingEnabled(
        IsEnvironmentEnabled("OUTERWILDS_CONTACT_DEBUG"));
    
    m_Scene = m_Physics->createScene(sceneDesc);
    if (!m_Scene) {
        DebugManager::GetInstance().Critical("PhysX", "createScene failed");
        return false;
    }

    if (auto* pvdClient = m_Scene->getScenePvdClient()) {
        pvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
        pvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
        pvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
    }

    // Create default material
    m_DefaultMaterial = m_Physics->createMaterial(0.5f, 0.5f, 0.6f);

    // Create character controller manager
    m_ControllerManager = PxCreateControllerManager(*m_Scene);
    if (!m_ControllerManager) {
        DebugManager::GetInstance().Critical("PhysX", "PxCreateControllerManager failed");
        return false;
    }

    DebugManager::GetInstance().Info("PhysX", "PhysX initialized successfully");
    return true;
}

void PhysXManager::Update(float deltaTime) {
    if (!m_Scene || deltaTime <= 0.0f) return;
    
    // 固定时间步
    // TimeManager clamps the frame once so all simulation systems advance by
    // exactly the same amount of time.
    
    // 模拟
    m_ContactReportCallback.BeginFrame();
    m_Scene->simulate(deltaTime);
    m_Scene->fetchResults(true);

    auto& diagnostics = DebugManager::GetInstance();
    diagnostics.SetMetric("PhysX rigid actors", GetRigidActorCount(), "actors");
    diagnostics.SetMetric("PhysX debug lines", GetDebugLineCount(), "lines");
    const ContactFrameStats contacts = m_ContactReportCallback.ConsumeFrameStats();
    diagnostics.SetMetric("Contact pairs", contacts.pairCount, "pairs");
    diagnostics.SetMetric("Contact points", contacts.pointCount, "points");
    diagnostics.SetMetric("Contact max impulse", contacts.maxImpulse, "N*s");
    diagnostics.SetMetric("Contact min separation", contacts.minSeparation, "m");
    diagnostics.SetMetric("PhysX wakes", contacts.wakeCount, "actors");
    diagnostics.SetMetric("PhysX sleeps", contacts.sleepCount, "actors");
    if (m_DebugVisualizationEnabled && !m_DebugGeometryReported &&
        (GetDebugLineCount() > 0 || GetDebugTriangleCount() > 0)) {
        std::ostringstream message;
        message << "Debug geometry ready: " << GetDebugLineCount() << " lines, "
                << GetDebugTriangleCount() << " triangles";
        diagnostics.Info("PhysXDebugView", message.str());
        m_DebugGeometryReported = true;
    }
}

bool PhysXManager::ConnectPvd(const char* host, int port, unsigned int timeoutMs) {
    if (!m_Pvd) {
        m_PvdStatus = "PVD interface is unavailable";
        DebugManager::GetInstance().Error("PVD", m_PvdStatus);
        return false;
    }
    if (m_Pvd->isConnected()) {
        m_PvdStatus = "Connected";
        return true;
    }

    if (m_PvdTransport) {
        m_PvdTransport->release();
        m_PvdTransport = nullptr;
    }

    m_PvdTransport = physx::PxDefaultPvdSocketTransportCreate(host, port, timeoutMs);
    if (!m_PvdTransport) {
        m_PvdStatus = "Failed to create socket transport";
        DebugManager::GetInstance().Error("PVD", m_PvdStatus);
        return false;
    }

    if (!m_Pvd->connect(*m_PvdTransport, physx::PxPvdInstrumentationFlag::eALL)) {
        m_PvdTransport->release();
        m_PvdTransport = nullptr;
        std::ostringstream status;
        status << "No PVD receiver at " << host << ':' << port;
        m_PvdStatus = status.str();
        DebugManager::GetInstance().Warning("PVD", m_PvdStatus);
        return false;
    }

    std::ostringstream status;
    status << "Connected to " << host << ':' << port;
    m_PvdStatus = status.str();
    DebugManager::GetInstance().Info("PVD", m_PvdStatus);
    return true;
}

void PhysXManager::DisconnectPvd() {
    if (m_Pvd && m_Pvd->isConnected()) {
        m_Pvd->disconnect();
    }
    if (m_PvdTransport) {
        m_PvdTransport->release();
        m_PvdTransport = nullptr;
    }
    m_PvdStatus = m_Pvd ? "Ready, disconnected" : "Not initialized";
    DebugManager::GetInstance().Info("PVD", "Disconnected");
}

bool PhysXManager::IsPvdConnected() const {
    return m_Pvd && m_Pvd->isConnected();
}

void PhysXManager::SetDebugVisualizationEnabled(bool enabled) {
    m_DebugVisualizationEnabled = enabled;
    if (!enabled) {
        m_DebugGeometryReported = false;
    }
    if (!m_Scene) {
        return;
    }
    const float scale = enabled ? 1.0f : 0.0f;
    m_Scene->setVisualizationParameter(physx::PxVisualizationParameter::eSCALE, scale);
    m_Scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_SHAPES, scale);
    m_Scene->setVisualizationParameter(physx::PxVisualizationParameter::eACTOR_AXES, scale);
    m_Scene->setVisualizationParameter(physx::PxVisualizationParameter::eCONTACT_POINT, scale);
    m_Scene->setVisualizationParameter(physx::PxVisualizationParameter::eCONTACT_NORMAL, scale);
    DebugManager::GetInstance().Info(
        "PhysX", enabled ? "Built-in debug geometry collection enabled" : "Built-in debug geometry collection disabled");
}

physx::PxU32 PhysXManager::GetDebugLineCount() const {
    return m_Scene ? m_Scene->getRenderBuffer().getNbLines() : 0;
}

physx::PxU32 PhysXManager::GetDebugTriangleCount() const {
    return m_Scene ? m_Scene->getRenderBuffer().getNbTriangles() : 0;
}

physx::PxU32 PhysXManager::GetRigidActorCount() const {
    if (!m_Scene) {
        return 0;
    }
    return m_Scene->getNbActors(
        physx::PxActorTypeFlag::eRIGID_STATIC | physx::PxActorTypeFlag::eRIGID_DYNAMIC);
}

const physx::PxRenderBuffer* PhysXManager::GetDebugRenderBuffer() const {
    return m_Scene ? &m_Scene->getRenderBuffer() : nullptr;
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

    DisconnectPvd();

    if (m_Pvd) {
        m_Pvd->release();
        m_Pvd = nullptr;
    }

    if (m_Foundation) {
        m_Foundation->release();
        m_Foundation = nullptr;
    }

    DebugManager::GetInstance().Info("PhysX", "PhysX shutdown successfully");
}

} // namespace outer_wilds
