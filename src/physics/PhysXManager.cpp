#include "PhysXManager.h"
#include "../core/DebugManager.h"
#include <iostream>
#include <cmath>
#include <vector>

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

    // Create physics with adjusted tolerances for large objects (planet radius ~64m)
    physx::PxTolerancesScale tolerances;
    tolerances.length = 10.0f;  // 默认是1，对于64m的物体，设置为10
    tolerances.speed = 100.0f;  // 默认是10，提高以适应更快的速度
    
    m_Physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_Foundation, tolerances, true, m_Pvd);
    if (!m_Physics) {
        std::cerr << "PxCreatePhysics failed!" << std::endl;
        return false;
    }

    // Create scene
    physx::PxSceneDesc sceneDesc(m_Physics->getTolerancesScale());
    sceneDesc.gravity = physx::PxVec3(0.0f, 0.0f, 0.0f);  // 禁用全局重力，使用自定义星球重力系统
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
        // 使用固定时间步进行模拟，避免大 deltaTime 导致的不稳定
        const float fixedStep = 1.0f / 60.0f;  // 60 FPS 固定步长
        const int maxSubSteps = 4;  // 最多4个子步
        
        // 限制 deltaTime 避免极端情况
        float clampedDt = std::min(deltaTime, fixedStep * maxSubSteps);
        
        // === 调试：在 simulate 前检查所有动态物体 ===
        static int frameCount = 0;
        frameCount++;
        
        physx::PxU32 nbActors = m_Scene->getNbActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC);
        
        // 第一帧输出所有动态物体信息
        if (frameCount == 1 && nbActors > 0) {
            std::cout << "\n[PhysX] === DYNAMIC ACTORS SUMMARY (First Frame) ===" << std::endl << std::flush;
            std::cout << "Total dynamic actors: " << nbActors << std::endl << std::flush;
            
            std::vector<physx::PxRigidActor*> firstFrameActors(nbActors);
            m_Scene->getActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC, 
                reinterpret_cast<physx::PxActor**>(firstFrameActors.data()), nbActors);
            
            for (physx::PxU32 i = 0; i < nbActors; i++) {
                physx::PxRigidDynamic* dyn = firstFrameActors[i]->is<physx::PxRigidDynamic>();
                if (dyn) {
                    physx::PxTransform p = dyn->getGlobalPose();
                    std::cout << "  Actor " << i << ": pos=(" << p.p.x << ", " << p.p.y << ", " << p.p.z 
                              << ") mass=" << dyn->getMass() << "kg" << std::endl;
                    
                    // 输出形状数量
                    physx::PxU32 numShapes = dyn->getNbShapes();
                    std::cout << "    Shapes: " << numShapes << std::endl;
                    
                    // 用 userData 检查
                    void* userData = dyn->userData;
                    std::cout << "    userData: " << (userData ? "SET" : "NULL") << std::endl << std::flush;
                }
            }
            std::cout << "[PhysX] === END SUMMARY ===" << std::endl << std::endl << std::flush;
        }
        
        if (nbActors > 0) {
            std::vector<physx::PxRigidActor*> actors(nbActors);
            m_Scene->getActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC, 
                reinterpret_cast<physx::PxActor**>(actors.data()), nbActors);
            
            for (physx::PxU32 i = 0; i < nbActors; i++) {
                physx::PxRigidDynamic* dynamic = actors[i]->is<physx::PxRigidDynamic>();
                if (dynamic) {
                    physx::PxTransform pose = dynamic->getGlobalPose();
                    physx::PxVec3 vel = dynamic->getLinearVelocity();
                    physx::PxVec3 angVel = dynamic->getAngularVelocity();
                    
                    // 检查 NaN 或极端值
                    bool hasNaN = std::isnan(pose.p.x) || std::isnan(pose.p.y) || std::isnan(pose.p.z) ||
                                  std::isnan(vel.x) || std::isnan(vel.y) || std::isnan(vel.z);
                    bool hasExtreme = vel.magnitude() > 1000.0f || angVel.magnitude() > 100.0f;
                    
                    if (hasNaN || hasExtreme) {
                        std::cout << "\n[PhysX WARNING] Frame " << frameCount 
                                  << " Actor " << i << " ABNORMAL STATE!" << std::endl;
                        std::cout << "  Position: (" << pose.p.x << ", " << pose.p.y << ", " << pose.p.z << ")" << std::endl;
                        std::cout << "  Velocity: (" << vel.x << ", " << vel.y << ", " << vel.z 
                                  << ") mag=" << vel.magnitude() << std::endl;
                        std::cout << "  AngVel: (" << angVel.x << ", " << angVel.y << ", " << angVel.z 
                                  << ") mag=" << angVel.magnitude() << std::endl;
                    }
                    
                    // 每100帧打印一次正常状态，或者当接近地面时更频繁
                    float distFromOrigin = sqrtf(pose.p.x * pose.p.x + pose.p.y * pose.p.y + pose.p.z * pose.p.z);
                    bool nearSurface = distFromOrigin < 70.0f;  // 接近64m半径的星球表面
                    
                    // 对于飞船（i==1），当接近碰撞距离时每帧报告
                    bool isSpacecraft = (i == 1);
                    bool isTestSphere = (i == 2);  // 测试球体
                    float collisionDist = isTestSphere ? (64.0f + 1.0f) : (64.0f + 3.0f);  // 星球半径 + 球体半径
                    bool veryNearCollision = (isSpacecraft || isTestSphere) && (distFromOrigin < collisionDist + 5.0f);
                    
                    if (frameCount % 100 == 0 || (nearSurface && frameCount % 10 == 0) || veryNearCollision) {
                        std::cout << "\n[PhysX] Frame " << frameCount 
                                  << " Actor " << i 
                                  << (isSpacecraft ? " (SPACECRAFT)" : (isTestSphere ? " (TEST_SPHERE)" : "")) 
                                  << " Status:" << std::endl;
                        std::cout << "  Position: (" << pose.p.x << ", " << pose.p.y << ", " << pose.p.z 
                                  << ") dist=" << distFromOrigin 
                                  << " (collision at " << collisionDist << ")" << std::endl;
                        std::cout << "  Velocity: (" << vel.x << ", " << vel.y << ", " << vel.z 
                                  << ") mag=" << vel.magnitude() << std::endl;
                    }
                }
            }
        }
        
        std::cout << "[PhysX] simulate(" << clampedDt << ")..." << std::flush;
        m_Scene->simulate(clampedDt);
        std::cout << " done. fetchResults..." << std::flush;
        m_Scene->fetchResults(true);
        std::cout << " done." << std::endl;
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