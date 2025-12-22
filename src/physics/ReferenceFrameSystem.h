#pragma once
#include "../core/ECS.h"
#include "../core/DebugManager.h"
#include "components/ReferenceFrameComponent.h"
#include "components/GravityAffectedComponent.h"
#include "components/GravityAlignmentComponent.h"
#include "components/RigidBodyComponent.h"
#include "components/GravitySourceComponent.h"
#include "../scene/components/TransformComponent.h"
#include "../gameplay/components/CharacterControllerComponent.h"
#include "../gameplay/components/SpacecraftComponent.h"
#include "PhysXManager.h"
#include <DirectXMath.h>
#include <PxPhysicsAPI.h>
#include <iostream>
#include <algorithm>

namespace outer_wilds {

/**
 * 参考系系统（简化版）
 * 
 * 核心设计思想：
 * 游戏内物体分为两类：
 * 1. 普通刚体：强制静态吸附，不参与物理模拟（规避 PhysX 无法单独设置参考系的问题）
 * 2. 飞船：只在玩家驾驶时启用物理模拟，下船后触地吸附
 * 
 * 工作流程：
 * 1. 自动附着物体到当前重力源（天体）
 * 2. 普通刚体：直接设置 staticAttach = true，由本系统管理位置
 * 3. 飞船：由 DynamicAttachSystem 管理（驾驶时物理模拟，触地后吸附）
 * 
 * 本地坐标方案（消除累积误差）：
 * - 首次附着时：计算 localOffset = 物体位置 - 天体位置
 * - 每帧更新时：物体位置 = 天体位置 + localOffset
 * 
 * 更新顺序：
 * - 必须在 OrbitSystem 之后（OrbitSystem 更新天体位置）
 * - 必须在 PhysicsSystem 之前
 */
class ReferenceFrameSystem : public System {
public:
    void Update(float deltaTime, entt::registry& registry) override {
        using namespace DirectX;
        using namespace components;
        
        // 保存 deltaTime 用于速度计算
        m_lastDeltaTime = deltaTime;
        
        // === 阶段1: 更新参考系状态 ===
        UpdateReferenceFrameState(registry);
        
        // === 阶段2: 自动附着物体到当前重力源 ===
        AutoAttachToGravitySource(registry);
        
        // === 阶段3: 强制普通刚体静态吸附（飞船除外） ===
        EnforceStaticAttachForNonSpacecraft(registry);
        
        // === 阶段4: 检测飞船触地吸附条件 ===
        CheckSpacecraftGroundAttach(deltaTime, registry);
        
        // === 阶段5: 应用参考系移动到附着物体 ===
        ApplyReferenceFrameMovement(deltaTime, registry);
    }
    
    /**
     * 物理模拟后调用：更新飞船的本地坐标
     * 只有飞船在驾驶模式下需要更新（它们参与物理模拟）
     */
    void PostPhysicsUpdate(entt::registry& registry) {
        using namespace DirectX;
        using namespace components;
        
        auto view = registry.view<AttachedToReferenceFrameComponent>();
        
        for (auto entity : view) {
            auto& attached = view.get<AttachedToReferenceFrameComponent>(entity);
            
            // 只处理飞船且非静态吸附的情况（即正在驾驶中的飞船）
            if (!attached.isSpacecraft) continue;
            if (attached.staticAttach) continue;
            if (attached.referenceFrame == entt::null) continue;
            
            // 获取参考系位置
            auto* refTransform = registry.try_get<TransformComponent>(attached.referenceFrame);
            if (!refTransform) continue;
            
            XMVECTOR refPos = XMLoadFloat3(&refTransform->position);
            
            // 获取飞船当前位置（物理模拟后的位置）
            auto* rigidBody = registry.try_get<RigidBodyComponent>(entity);
            if (rigidBody && rigidBody->physxActor) {
                if (rigidBody->isKinematic) continue;
                
                physx::PxRigidDynamic* dynamicActor = rigidBody->physxActor->is<physx::PxRigidDynamic>();
                if (dynamicActor) {
                    physx::PxVec3 pos = dynamicActor->getGlobalPose().p;
                    XMVECTOR objectPos = XMVectorSet(pos.x, pos.y, pos.z, 0.0f);
                    
                    // 更新本地坐标
                    XMVECTOR newLocalOffset = XMVectorSubtract(objectPos, refPos);
                    XMStoreFloat3(&attached.localOffset, newLocalOffset);
                }
            }
        }
        
        // 处理玩家 CharacterController
        auto ccView = registry.view<AttachedToReferenceFrameComponent, CharacterControllerComponent>();
        for (auto entity : ccView) {
            auto& attached = ccView.get<AttachedToReferenceFrameComponent>(entity);
            auto& character = ccView.get<CharacterControllerComponent>(entity);
            
            if (attached.staticAttach) continue;
            if (attached.referenceFrame == entt::null) continue;
            if (!character.controller) continue;
            
            auto* refTransform = registry.try_get<TransformComponent>(attached.referenceFrame);
            if (!refTransform) continue;
            
            XMVECTOR refPos = XMLoadFloat3(&refTransform->position);
            physx::PxExtendedVec3 pos = character.controller->getFootPosition();
            XMVECTOR objectPos = XMVectorSet((float)pos.x, (float)pos.y, (float)pos.z, 0.0f);
            
            XMVECTOR newLocalOffset = XMVectorSubtract(objectPos, refPos);
            XMStoreFloat3(&attached.localOffset, newLocalOffset);
        }
    }
    
private:
    // 存储上一帧的时间步长用于计算速度
    float m_lastDeltaTime = 0.016f;
    
    /**
     * 阶段1: 更新参考系状态
     * 计算天体的位移增量和速度，供飞船起飞惯性使用
     */
    void UpdateReferenceFrameState(entt::registry& registry) {
        using namespace DirectX;
        using namespace components;
        
        auto view = registry.view<ReferenceFrameComponent, TransformComponent>();
        
        for (auto entity : view) {
            auto& refFrame = view.get<ReferenceFrameComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);
            
            if (!refFrame.isActive) continue;
            
            if (refFrame.isInitialized) {
                // 计算位移增量
                XMVECTOR lastPos = XMLoadFloat3(&refFrame.lastPosition);
                XMVECTOR currentPos = XMLoadFloat3(&transform.position);
                XMVECTOR delta = XMVectorSubtract(currentPos, lastPos);
                XMStoreFloat3(&refFrame.deltaPosition, delta);
                
                // 计算速度（米/秒）
                if (m_lastDeltaTime > 0.0001f) {
                    XMVECTOR currentVelocity = XMVectorScale(delta, 1.0f / m_lastDeltaTime);
                    XMVECTOR lastVelocity = XMLoadFloat3(&refFrame.lastVelocity);
                    XMStoreFloat3(&refFrame.velocity, currentVelocity);
                    
                    // 计算速度变化（用于参考系补偿）
                    XMVECTOR deltaVel = XMVectorSubtract(currentVelocity, lastVelocity);
                    XMStoreFloat3(&refFrame.deltaVelocity, deltaVel);
                    
                    // 保存为下一帧的 lastVelocity
                    refFrame.lastVelocity = refFrame.velocity;
                } else {
                    refFrame.deltaVelocity = {0.0f, 0.0f, 0.0f};
                }
            }
            
            // 记录当前位置
            refFrame.lastPosition = transform.position;
            refFrame.isInitialized = true;
        }
    }
    
    /**
     * 阶段2: 自动将物体附着到当前重力源
     */
    void AutoAttachToGravitySource(entt::registry& registry) {
        using namespace DirectX;
        using namespace components;
        
        auto view = registry.view<AttachedToReferenceFrameComponent, GravityAffectedComponent>();
        
        // 调试开关（设为false禁用输出）
        bool shouldDebug = false;
        
        for (auto entity : view) {
            auto& attached = view.get<AttachedToReferenceFrameComponent>(entity);
            auto& gravity = view.get<GravityAffectedComponent>(entity);
            
            if (!attached.autoAttach) continue;
            
            // 使用当前重力源作为参考系
            if (gravity.currentGravitySource != entt::null) {
                if (registry.all_of<ReferenceFrameComponent>(gravity.currentGravitySource)) {
                    Entity oldFrame = attached.referenceFrame;
                    Entity newFrame = gravity.currentGravitySource;
                    
                    // 如果参考系改变，重新计算本地坐标
                    if (oldFrame != newFrame) {
                        attached.referenceFrame = newFrame;
                        
                        // 计算本地坐标 = 物体位置 - 天体位置
                        auto* refTransform = registry.try_get<TransformComponent>(newFrame);
                        if (refTransform) {
                            XMVECTOR refPos = XMLoadFloat3(&refTransform->position);
                            XMVECTOR objectPos;
                            
                            // 获取物体位置
                            auto* rigidBody = registry.try_get<RigidBodyComponent>(entity);
                            if (rigidBody && rigidBody->physxActor) {
                                physx::PxVec3 pos = rigidBody->physxActor->getGlobalPose().p;
                                objectPos = XMVectorSet(pos.x, pos.y, pos.z, 0.0f);
                            } else {
                                auto* transform = registry.try_get<TransformComponent>(entity);
                                if (transform) {
                                    objectPos = XMLoadFloat3(&transform->position);
                                }
                            }
                            
                            XMVECTOR localOffset = XMVectorSubtract(objectPos, refPos);
                            XMStoreFloat3(&attached.localOffset, localOffset);
                            attached.isInitialized = true;
                            
                            if (shouldDebug) {
                                auto* refFrame = registry.try_get<ReferenceFrameComponent>(newFrame);
                                std::cout << "[RefFrame] Attached to: " << (refFrame ? refFrame->name : "Unknown")
                                         << ", localOffset=(" << attached.localOffset.x
                                         << "," << attached.localOffset.y
                                         << "," << attached.localOffset.z << ")" << std::endl;
                            }
                        }
                    }
                    
                    // 调试输出当前状态
                    if (shouldDebug) {
                        auto* refFrame = registry.try_get<ReferenceFrameComponent>(newFrame);
                        auto* rigidBody = registry.try_get<RigidBodyComponent>(entity);
                        
                        if (rigidBody && rigidBody->physxActor) {
                            physx::PxRigidDynamic* dynamicActor = rigidBody->physxActor->is<physx::PxRigidDynamic>();
                            if (dynamicActor) {
                                physx::PxVec3 vel = dynamicActor->getLinearVelocity();
                                float speed = vel.magnitude();
                                
                                std::cout << "[RefFrame] RigidBody on: " << (refFrame ? refFrame->name : "Unknown")
                                         << ", speed=" << speed << " m/s" << std::endl;
                            }
                        }
                    }
                }
            } else {
                // 没有重力源时的处理
                // 对于飞船：保持当前参考系，不清除（避免飞出重力范围后出问题）
                // 对于其他物体：清除参考系
                if (!attached.isSpacecraft) {
                    attached.referenceFrame = entt::null;
                    attached.isInitialized = false;
                }
                // 飞船保持原有参考系，继续飞行
            }
        }
    }
    
    /**
     * 阶段3: 强制普通刚体（非飞船）使用静态吸附
     * 
     * 设计理念：
     * - 除飞船外，所有刚体都强制静态吸附
     * - 规避 PhysX 不能为每个物体单独设置参考系的问题
     * - 飞船的状态由本系统的 CheckSpacecraftGroundAttach 管理
     */
    void EnforceStaticAttachForNonSpacecraft(entt::registry& registry) {
        using namespace components;
        
        auto view = registry.view<AttachedToReferenceFrameComponent, RigidBodyComponent>();
        
        for (auto entity : view) {
            auto& attached = view.get<AttachedToReferenceFrameComponent>(entity);
            auto& rigidBody = view.get<RigidBodyComponent>(entity);
            
            // 飞船由 CheckSpacecraftGroundAttach 管理，跳过
            if (attached.isSpacecraft) continue;
            
            // 强制非飞船刚体使用静态吸附
            if (!attached.staticAttach) {
                attached.staticAttach = true;
                
                // 设置为 Kinematic，停止物理模拟
                if (rigidBody.physxActor) {
                    physx::PxRigidDynamic* dynamicActor = rigidBody.physxActor->is<physx::PxRigidDynamic>();
                    if (dynamicActor) {
                        dynamicActor->setLinearVelocity(physx::PxVec3(0, 0, 0));
                        dynamicActor->setAngularVelocity(physx::PxVec3(0, 0, 0));
                        dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
                        rigidBody.isKinematic = true;
                    }
                }
            }
        }
    }
    
    /**
     * 阶段4: 检测飞船触地吸附条件
     * 
     * 飞船状态管理：
     * - 驾驶中 (PILOTED/LANDING_ASSIST): 正常物理模拟
     * - 空闲 (IDLE): 检测触地条件，满足则切换到静态吸附
     * 
     * 触地吸附条件：
     * 1. 接地（射线检测）
     * 2. 速度低于阈值
     * 3. 稳定时间超过阈值
     */
    void CheckSpacecraftGroundAttach(float deltaTime, entt::registry& registry) {
        using namespace DirectX;
        using namespace components;
        
        auto* pxScene = PhysXManager::GetInstance().GetScene();
        if (!pxScene) return;
        
        const float groundCheckDistance = 5.0f;
        const float velocityThreshold = 2.0f;
        const float stableTimeRequired = 0.1f;
        
        auto view = registry.view<AttachedToReferenceFrameComponent, SpacecraftComponent,
                                  RigidBodyComponent, GravityAffectedComponent>();
        
        for (auto entity : view) {
            auto& attached = view.get<AttachedToReferenceFrameComponent>(entity);
            auto& spacecraft = view.get<SpacecraftComponent>(entity);
            auto& rigidBody = view.get<RigidBodyComponent>(entity);
            auto& gravity = view.get<GravityAffectedComponent>(entity);
            
            if (!attached.isSpacecraft) continue;
            if (!rigidBody.physxActor) continue;
            
            physx::PxRigidDynamic* dynamicActor = rigidBody.physxActor->is<physx::PxRigidDynamic>();
            if (!dynamicActor) continue;
            
            physx::PxRigidBodyFlags flags = dynamicActor->getRigidBodyFlags();
            
            // 正在驾驶：仅当已脱离静态吸附时才保持动态
            if (spacecraft.currentState != SpacecraftComponent::State::IDLE) {
                if (attached.staticAttach) {
                    // 等待玩家输入起飞，保持静态
                    if (!(flags & physx::PxRigidBodyFlag::eKINEMATIC)) {
                        // 在设置为 kinematic 之前清零速度
                        dynamicActor->setLinearVelocity(physx::PxVec3(0, 0, 0));
                        dynamicActor->setAngularVelocity(physx::PxVec3(0, 0, 0));
                        dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
                        rigidBody.isKinematic = true;
                    }
                    // 注意：kinematic 物体不能调用 setLinearVelocity/setAngularVelocity
                } else {
                    attached.groundStableTime = 0.0f;
                    if (rigidBody.isKinematic || (flags & physx::PxRigidBodyFlag::eKINEMATIC)) {
                        dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, false);
                        rigidBody.isKinematic = false;
                        dynamicActor->wakeUp();
                    }
                }
                continue;
            }
            
            // 无人驾驶：如果已经静态吸附则维持
            if (attached.staticAttach) {
                if (!(flags & physx::PxRigidBodyFlag::eKINEMATIC)) {
                    // 在设置为 kinematic 之前清零速度
                    dynamicActor->setLinearVelocity(physx::PxVec3(0, 0, 0));
                    dynamicActor->setAngularVelocity(physx::PxVec3(0, 0, 0));
                    dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
                    rigidBody.isKinematic = true;
                }
                // 注意：kinematic 物体不能调用 setLinearVelocity/setAngularVelocity
                continue;
            }
            
            // 检测接地
            physx::PxVec3 actorPos = dynamicActor->getGlobalPose().p;
            physx::PxVec3 rayDir(gravity.currentGravityDir.x,
                                 gravity.currentGravityDir.y,
                                 gravity.currentGravityDir.z);
            if (rayDir.normalize() < 1e-3f) {
                rayDir = physx::PxVec3(0.0f, -1.0f, 0.0f);
            }
            
            physx::PxRaycastBuffer hitBuffer;
            physx::PxQueryFilterData filterData;
            filterData.flags = physx::PxQueryFlag::eSTATIC;
            bool isGrounded = pxScene->raycast(actorPos, rayDir, groundCheckDistance,
                                               hitBuffer, physx::PxHitFlag::eDEFAULT, filterData);
            
            float speed = dynamicActor->getLinearVelocity().magnitude();
            bool isStable = speed < velocityThreshold;
            
            if (isGrounded && isStable) {
                attached.groundStableTime += deltaTime;
                if (attached.groundStableTime >= stableTimeRequired) {
                    SwitchSpacecraftToStaticAttach(entity, attached, rigidBody, dynamicActor, registry);
                }
            } else {
                attached.groundStableTime = 0.0f;
            }
        }
    }
    
    /**
     * 将飞船切换到静态吸附模式
     */
    void SwitchSpacecraftToStaticAttach(Entity entity,
                                        AttachedToReferenceFrameComponent& attached,
                                        RigidBodyComponent& rigidBody,
                                        physx::PxRigidDynamic* dynamicActor,
                                        entt::registry& registry) {
        using namespace DirectX;
        using namespace components;
        
        // 记录当前位置作为本地坐标
        if (attached.referenceFrame != entt::null) {
            auto* refTransform = registry.try_get<TransformComponent>(attached.referenceFrame);
            if (refTransform) {
                physx::PxVec3 pos = dynamicActor->getGlobalPose().p;
                
                XMVECTOR refPos = XMLoadFloat3(&refTransform->position);
                XMVECTOR objPos = XMVectorSet(pos.x, pos.y, pos.z, 0.0f);
                XMVECTOR localOffset = XMVectorSubtract(objPos, refPos);
                
                XMStoreFloat3(&attached.localOffset, localOffset);
                attached.isInitialized = true;
            }
        }
        
        // 启用静态吸附模式
        attached.staticAttach = true;
        attached.groundStableTime = 0.0f;
        
        // 清零速度
        dynamicActor->setLinearVelocity(physx::PxVec3(0, 0, 0));
        dynamicActor->setAngularVelocity(physx::PxVec3(0, 0, 0));
        
        // 设置为 Kinematic
        dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
        rigidBody.isKinematic = true;
    }
    
    /**
     * 阶段5: 应用参考系移动
     * 
     * 核心逻辑：
     * - 物体全局位置 = 天体当前位置 + 本地坐标
     * - 这样天体移动时，物体自动跟随，无累积误差
     * 
     * 静态吸附模式（staticAttach = true）：
     * - 只更新 Transform，完全不触碰 PhysX
     * - 适用于装饰物体
     */
    void ApplyReferenceFrameMovement(float deltaTime, entt::registry& registry) {
        using namespace DirectX;
        using namespace components;
        
        auto view = registry.view<AttachedToReferenceFrameComponent>();
        
        // 详细调试：每帧输出
        static int frameCount = 0;
        bool shouldDebug = (++frameCount % 60 == 0); // 每秒输出一次
        
        for (auto entity : view) {
            auto& attached = view.get<AttachedToReferenceFrameComponent>(entity);
            
            // 飞船调试输出
            if (attached.isSpacecraft && shouldDebug) {
                auto* transform = registry.try_get<TransformComponent>(entity);
                auto* rigidBody = registry.try_get<RigidBodyComponent>(entity);
                
                std::cout << "[Spacecraft Debug] ";
                std::cout << "staticAttach=" << attached.staticAttach;
                std::cout << ", isInitialized=" << attached.isInitialized;
                std::cout << ", refFrame=" << (attached.referenceFrame != entt::null ? "OK" : "NULL");
                std::cout << ", enableComp=" << attached.enableCompensation;
                
                if (transform) {
                    std::cout << ", pos=(" << transform->position.x << "," 
                             << transform->position.y << "," << transform->position.z << ")";
                }
                if (rigidBody) {
                    std::cout << ", isKinematic=" << rigidBody->isKinematic;
                    if (rigidBody->physxActor) {
                        physx::PxVec3 pxPos = rigidBody->physxActor->getGlobalPose().p;
                        std::cout << ", pxPos=(" << pxPos.x << "," << pxPos.y << "," << pxPos.z << ")";
                    }
                }
                std::cout << std::endl;
                
                // 输出本地偏移量
                std::cout << "[Spacecraft Debug] localOffset=(" << attached.localOffset.x 
                         << "," << attached.localOffset.y << "," << attached.localOffset.z << ")";
                if (attached.referenceFrame != entt::null) {
                    auto* refTransform = registry.try_get<TransformComponent>(attached.referenceFrame);
                    if (refTransform) {
                        std::cout << ", refPos=(" << refTransform->position.x << ","
                                 << refTransform->position.y << "," << refTransform->position.z << ")";
                    }
                }
                std::cout << std::endl;
            }
            
            if (!attached.enableCompensation) continue;
            if (attached.referenceFrame == entt::null) continue;
            if (!attached.isInitialized) continue;
            
            // 获取参考系当前位置
            auto* refTransform = registry.try_get<TransformComponent>(attached.referenceFrame);
            if (!refTransform) continue;
            
            XMVECTOR refPos = XMLoadFloat3(&refTransform->position);
            XMVECTOR localOffset = XMLoadFloat3(&attached.localOffset);
            
            // 计算目标全局位置
            XMVECTOR targetPos = XMVectorAdd(refPos, localOffset);
            XMFLOAT3 targetPosF;
            XMStoreFloat3(&targetPosF, targetPos);
            
            // === 静态吸附模式：更新 Transform 和 PhysX 位置 ===
            if (attached.staticAttach) {
                auto* transform = registry.try_get<TransformComponent>(entity);
                if (transform) {
                    transform->position = targetPosF;
                    
                    // 同步到 PhysX（避免 Transform 和 PhysX 不一致）
                    auto* rigidBody = registry.try_get<RigidBodyComponent>(entity);
                    if (rigidBody && rigidBody->physxActor) {
                        physx::PxRigidDynamic* dynamicActor = rigidBody->physxActor->is<physx::PxRigidDynamic>();
                        if (dynamicActor) {
                            // 确保是 Kinematic 模式
                            if (!rigidBody->isKinematic) {
                                // 先清零速度（必须在设置 kinematic 之前）
                                dynamicActor->setLinearVelocity(physx::PxVec3(0, 0, 0));
                                dynamicActor->setAngularVelocity(physx::PxVec3(0, 0, 0));
                                dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
                                rigidBody->isKinematic = true;
                            }
                            
                            // 直接设置位置（Kinematic模式下用setGlobalPose）
                            physx::PxTransform pose = dynamicActor->getGlobalPose();
                            pose.p = physx::PxVec3(targetPosF.x, targetPosF.y, targetPosF.z);
                            dynamicActor->setGlobalPose(pose);
                            
                            // 注意：kinematic 物体不能调用 setLinearVelocity/setAngularVelocity
                        }
                    }
                    
                    // 计算重力对齐旋转（只更新 Transform 的旋转）
                    // 注意：飞船由 SpacecraftControlSystem 控制旋转，这里不覆盖
                    if (!attached.isSpacecraft) {
                        auto* gravity = registry.try_get<GravityAffectedComponent>(entity);
                        if (gravity && gravity->affectedByGravity) {
                            XMVECTOR gravityDir = XMLoadFloat3(&gravity->currentGravityDir);
                            XMVECTOR upDir = XMVectorNegate(gravityDir);
                            upDir = XMVector3Normalize(upDir);
                            
                            XMVECTOR defaultUp = XMVectorSet(0, 1, 0, 0);
                            XMVECTOR rotQuat = XMQuaternionIdentity();
                            float dot = XMVectorGetX(XMVector3Dot(defaultUp, upDir));
                            
                            if (dot < -0.9999f) {
                                rotQuat = XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), XM_PI);
                            } else if (dot < 0.9999f) {
                                XMVECTOR axis = XMVector3Cross(defaultUp, upDir);
                                axis = XMVector3Normalize(axis);
                                float angle = acosf(dot);
                                rotQuat = XMQuaternionRotationAxis(axis, angle);
                            }
                            
                            XMFLOAT4 rotQuatF;
                            XMStoreFloat4(&rotQuatF, rotQuat);
                            transform->rotation = rotQuatF;
                        }
                    }
                    
                    if (shouldDebug) {
                        std::cout << "[RefFrame-Static] Transform updated: pos=(" 
                                 << targetPosF.x << "," << targetPosF.y << "," << targetPosF.z << ")" << std::endl;
                    }
                }
                continue;  // 静态吸附完成，跳过后续处理
            }
            
            // === 动态模式：不应用任何补偿 ===
            // 飞船飞行时完全按照惯性系物理模拟，不受参考系影响
            // 这样飞船可以自由加速，不会被限制在轨道速度
            auto* spacecraft = registry.try_get<SpacecraftComponent>(entity);
            if (spacecraft && spacecraft->currentState != SpacecraftComponent::State::IDLE) {
                // 飞船正在飞行，不应用任何参考系补偿
                // 物理引擎会自然处理重力和推力
                continue;
            }
            
            // === CharacterController：直接设置位置 ===
            auto* character = registry.try_get<CharacterControllerComponent>(entity);
            if (character && character->controller) {
                character->controller->setFootPosition(
                    physx::PxExtendedVec3(targetPosF.x, targetPosF.y, targetPosF.z)
                );
                
                if (shouldDebug) {
                    std::cout << "[RefFrame] CharacterController moved to (" 
                             << targetPosF.x << "," << targetPosF.y << "," << targetPosF.z << ")" << std::endl;
                }
                continue;
            }
            
            // === 其他Transform物体：直接设置位置 ===
            auto* transform = registry.try_get<TransformComponent>(entity);
            if (transform) {
                transform->position = targetPosF;
            }
        }
    }
};

} // namespace outer_wilds
