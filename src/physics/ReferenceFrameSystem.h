#pragma once
#include "../core/ECS.h"
#include "../core/DebugManager.h"
#include "components/ReferenceFrameComponent.h"
#include "components/GravityAffectedComponent.h"
#include "components/GravityAlignmentComponent.h"
#include "components/RigidBodyComponent.h"
#include "../scene/components/TransformComponent.h"
#include "../gameplay/components/CharacterControllerComponent.h"
#include <DirectXMath.h>
#include <PxPhysicsAPI.h>
#include <iostream>
#include <algorithm>

namespace outer_wilds {

/**
 * 参考系系统（本地坐标方案）- 消除累积误差
 * 
 * 核心设计：
 * - 不再使用位移增量补偿（会累积圆弧vs弦长误差）
 * - 改为存储物体相对于天体的【本地坐标】
 * - 每帧直接计算：全局位置 = 天体位置 + 本地坐标
 * 
 * 工作流程：
 * 1. 首次附着时：计算并存储 localOffset = 物体位置 - 天体位置
 * 2. 每帧更新时：物体位置 = 天体位置 + localOffset
 * 3. PhysX正常模拟：localOffset会被物理运动更新
 * 
 * 关键点：
 * - 本地坐标在物理模拟后更新（允许物理移动）
 * - 天体移动时，物体跟随移动（无累积误差）
 * 
 * 更新顺序：
 * - 必须在OrbitSystem之后（OrbitSystem更新天体位置）
 * - 必须在PhysicsSystem之前（应用天体移动）
 * - PhysicsSystem之后还需要更新localOffset
 */
class ReferenceFrameSystem : public System {
public:
    void Update(float deltaTime, entt::registry& registry) override {
        using namespace DirectX;
        using namespace components;
        
        // === 阶段1: 更新参考系状态（记录位置供其他系统使用） ===
        UpdateReferenceFrameState(registry);
        
        // === 阶段2: 自动附着物体到当前重力源 ===
        AutoAttachToGravitySource(registry);
        
        // === 阶段3: 应用天体移动到附着物体 ===
        ApplyReferenceFrameMovement(deltaTime, registry);
    }
    
    /**
     * 物理模拟后调用：更新物体的本地坐标
     * 这允许物理运动修改物体在天体表面的位置
     */
    void PostPhysicsUpdate(entt::registry& registry) {
        using namespace DirectX;
        using namespace components;
        
        auto view = registry.view<AttachedToReferenceFrameComponent>();
        
        static int frameCount = 0;
        bool shouldDebug = (++frameCount % 60 == 0);
        
        for (auto entity : view) {
            auto& attached = view.get<AttachedToReferenceFrameComponent>(entity);
            
            if (attached.referenceFrame == entt::null) continue;
            
            // 获取参考系位置
            auto* refTransform = registry.try_get<TransformComponent>(attached.referenceFrame);
            if (!refTransform) continue;
            
            XMVECTOR refPos = XMLoadFloat3(&refTransform->position);
            
            // 获取物体当前位置（物理模拟后的位置）
            XMVECTOR objectPos;
            
            auto* rigidBody = registry.try_get<RigidBodyComponent>(entity);
            if (rigidBody && rigidBody->physxActor) {
                physx::PxRigidDynamic* dynamicActor = rigidBody->physxActor->is<physx::PxRigidDynamic>();
                if (dynamicActor) {
                    physx::PxVec3 pos = dynamicActor->getGlobalPose().p;
                    physx::PxVec3 vel = dynamicActor->getLinearVelocity();
                    objectPos = XMVectorSet(pos.x, pos.y, pos.z, 0.0f);
                    
                    // 更新本地坐标
                    XMFLOAT3 oldOffset = attached.localOffset;
                    XMVECTOR newLocalOffset = XMVectorSubtract(objectPos, refPos);
                    XMStoreFloat3(&attached.localOffset, newLocalOffset);
                    
                    if (shouldDebug) {
                        float speed = vel.magnitude();
                        std::cout << "[RefFrame-Post] RigidBody: pos=(" << pos.x << "," << pos.y << "," << pos.z
                                 << "), vel=(" << vel.x << "," << vel.y << "," << vel.z << "), speed=" << speed << " m/s"
                                 << ", oldLocalOffset=(" << oldOffset.x << "," << oldOffset.y << "," << oldOffset.z
                                 << "), newLocalOffset=(" << attached.localOffset.x << "," << attached.localOffset.y << "," << attached.localOffset.z << ")" << std::endl;
                    }
                    continue;
                }
            }
            
            auto* character = registry.try_get<CharacterControllerComponent>(entity);
            if (character && character->controller) {
                physx::PxExtendedVec3 pos = character->controller->getFootPosition();
                objectPos = XMVectorSet((float)pos.x, (float)pos.y, (float)pos.z, 0.0f);
                
                // 更新本地坐标
                XMVECTOR newLocalOffset = XMVectorSubtract(objectPos, refPos);
                XMStoreFloat3(&attached.localOffset, newLocalOffset);
                continue;
            }
        }
    }
    
private:
    /**
     * 阶段1: 更新参考系状态
     */
    void UpdateReferenceFrameState(entt::registry& registry) {
        using namespace components;
        
        auto view = registry.view<ReferenceFrameComponent, TransformComponent>();
        
        for (auto entity : view) {
            auto& refFrame = view.get<ReferenceFrameComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);
            
            if (!refFrame.isActive) continue;
            
            // 记录当前位置（供调试和其他系统使用）
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
                attached.referenceFrame = entt::null;
                attached.isInitialized = false;
            }
        }
    }
    
    /**
     * 阶段3: 应用参考系移动
     * 
     * 核心逻辑：
     * - 物体全局位置 = 天体当前位置 + 本地坐标
     * - 这样天体移动时，物体自动跟随，无累积误差
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
            
            // 应用到RigidBody
            auto* rigidBody = registry.try_get<RigidBodyComponent>(entity);
            if (rigidBody && rigidBody->physxActor) {
                physx::PxRigidDynamic* dynamicActor = rigidBody->physxActor->is<physx::PxRigidDynamic>();
                if (dynamicActor) {
                    physx::PxVec3 oldPos = dynamicActor->getGlobalPose().p;
                    physx::PxVec3 vel = dynamicActor->getLinearVelocity();
                    physx::PxQuat oldRot = dynamicActor->getGlobalPose().q;
                    
                    // === 灵活的重力对齐系统 ===
                    auto* alignmentComp = registry.try_get<GravityAlignmentComponent>(entity);
                    auto* gravity = registry.try_get<GravityAffectedComponent>(entity);
                    physx::PxQuat newRot = oldRot; // 默认保持原旋转
                    
                    // 检查是否需要对齐
                    bool shouldAlign = false;
                    if (alignmentComp && gravity && gravity->affectedByGravity) {
                        using AlignMode = GravityAlignmentComponent::AlignmentMode;
                        switch (alignmentComp->mode) {
                            case AlignMode::FORCE_ALIGN:
                                shouldAlign = true;  // 箱子/岩石：总是对齐
                                break;
                            case AlignMode::ASSIST_ALIGN:
                                shouldAlign = alignmentComp->assistAlignActive;  // 飞船：玩家激活时对齐
                                break;
                            case AlignMode::NONE:
                            default:
                                shouldAlign = false;  // 完全自由
                                break;
                        }
                    } else if (!alignmentComp && gravity && gravity->affectedByGravity) {
                        // 没有对齐组件时，默认行为：对齐（向后兼容）
                        shouldAlign = true;
                    }
                    
                    if (shouldAlign) {
                        // 计算对齐旋转
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
                        
                        // 平滑过渡（如果设置了对齐速度）
                        if (alignmentComp && alignmentComp->alignmentSpeed > 0.0f) {
                            XMVECTOR currentRotQuat = XMVectorSet(oldRot.x, oldRot.y, oldRot.z, oldRot.w);
                            float t = (std::min)(1.0f, alignmentComp->alignmentSpeed * deltaTime);
                            rotQuat = XMQuaternionSlerp(currentRotQuat, rotQuat, t);
                        }
                        
                        XMFLOAT4 rotQuatF;
                        XMStoreFloat4(&rotQuatF, rotQuat);
                        newRot = physx::PxQuat(rotQuatF.x, rotQuatF.y, rotQuatF.z, rotQuatF.w);
                    }
                    
                    // 设置新的位置和旋转
                    physx::PxTransform pose(
                        physx::PxVec3(targetPosF.x, targetPosF.y, targetPosF.z),
                        newRot
                    );
                    dynamicActor->setGlobalPose(pose);
                    
                    if (shouldDebug) {
                        float speed = vel.magnitude();
                        const char* alignStatus = shouldAlign ? "ALIGNED" : "FREE";
                        if (alignmentComp) {
                            using AlignMode = GravityAlignmentComponent::AlignmentMode;
                            switch (alignmentComp->mode) {
                                case AlignMode::FORCE_ALIGN: alignStatus = "FORCE"; break;
                                case AlignMode::ASSIST_ALIGN: alignStatus = alignmentComp->assistAlignActive ? "ASSIST_ON" : "ASSIST_OFF"; break;
                                case AlignMode::NONE: alignStatus = "FREE"; break;
                            }
                        }
                        std::cout << "[RefFrame-Apply] RigidBody: pos=(" << targetPosF.x << "," << targetPosF.y << "," << targetPosF.z
                                 << "), vel=(" << vel.x << "," << vel.y << "," << vel.z << "), speed=" << speed << " m/s"
                                 << ", alignMode=" << alignStatus << std::endl;
                    }
                    continue;
                }
            }
            
            // 应用到CharacterController
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
            
            // 直接修改Transform
            auto* transform = registry.try_get<TransformComponent>(entity);
            if (transform) {
                transform->position = targetPosF;
            }
        }
    }
};

} // namespace outer_wilds
