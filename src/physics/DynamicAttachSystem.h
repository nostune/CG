#pragma once
#include "../core/ECS.h"
#include "components/DynamicAttachComponent.h"
#include "components/ReferenceFrameComponent.h"
#include "components/GravityAffectedComponent.h"
#include "components/RigidBodyComponent.h"
#include "../scene/components/TransformComponent.h"
#include "PhysXManager.h"
#include <DirectXMath.h>
#include <PxPhysicsAPI.h>
#include <iostream>

namespace outer_wilds {

/**
 * 动态吸附系统 - 管理动态刚体的状态切换
 * 
 * 工作流程：
 * 1. 检测刚体是否接地（射线检测）
 * 2. 检测刚体速度是否足够低
 * 3. 满足条件持续一段时间后，切换到 GROUNDED 状态
 * 4. GROUNDED 状态下：禁用物理，使用本地坐标跟随星球
 * 5. 受到干扰时：切换回 DYNAMIC 状态，恢复物理
 * 
 * 更新顺序：
 * - 在 ReferenceFrameSystem 之前运行（决定物体是否使用静态吸附）
 * - 或者直接集成到 ReferenceFrameSystem 中
 */
class DynamicAttachSystem : public System {
public:
    void Update(float deltaTime, entt::registry& registry) override {
        using namespace DirectX;
        using namespace components;
        
        auto* pxScene = PhysXManager::GetInstance().GetScene();
        if (!pxScene) return;
        
        auto view = registry.view<DynamicAttachComponent, RigidBodyComponent, 
                                  GravityAffectedComponent, AttachedToReferenceFrameComponent>();
        
        static int frameCount = 0;
        bool shouldDebug = (++frameCount % 60 == 0);
        
        for (auto entity : view) {
            auto& dynAttach = view.get<DynamicAttachComponent>(entity);
            auto& rigidBody = view.get<RigidBodyComponent>(entity);
            auto& gravity = view.get<GravityAffectedComponent>(entity);
            auto& attached = view.get<AttachedToReferenceFrameComponent>(entity);
            
            if (!rigidBody.physxActor) continue;
            
            physx::PxRigidDynamic* dynamicActor = rigidBody.physxActor->is<physx::PxRigidDynamic>();
            if (!dynamicActor) continue;

            // 飞船由 ReferenceFrameSystem 的 CheckSpacecraftGroundAttach 管理
            // DynamicAttachSystem 不应该干扰飞船的静态吸附状态
            if (attached.isSpacecraft) {
                continue;
            }
            
            // 获取当前速度
            physx::PxVec3 vel = dynamicActor->getLinearVelocity();
            float speed = vel.magnitude();
            
            // 根据当前状态处理
            switch (dynAttach.currentState) {
                case DynamicAttachComponent::State::DYNAMIC:
                    UpdateDynamicState(deltaTime, entity, dynAttach, rigidBody, gravity, 
                                      attached, dynamicActor, pxScene, registry, shouldDebug);
                    break;
                    
                case DynamicAttachComponent::State::GROUNDED:
                    UpdateGroundedState(deltaTime, entity, dynAttach, rigidBody, gravity,
                                       attached, dynamicActor, registry, shouldDebug);
                    break;
            }
        }
    }
    
private:
    /**
     * DYNAMIC 状态更新：检测是否应该切换到 GROUNDED
     */
    void UpdateDynamicState(float deltaTime, Entity entity,
                           DynamicAttachComponent& dynAttach,
                           RigidBodyComponent& rigidBody,
                           GravityAffectedComponent& gravity,
                           AttachedToReferenceFrameComponent& attached,
                           physx::PxRigidDynamic* dynamicActor,
                           physx::PxScene* pxScene,
                           entt::registry& registry,
                           bool shouldDebug) {
        using namespace DirectX;
        
        // 1. 检测是否接地
        physx::PxVec3 actorPos = dynamicActor->getGlobalPose().p;
        XMFLOAT3 gravDir = gravity.currentGravityDir;
        physx::PxVec3 rayDir(gravDir.x, gravDir.y, gravDir.z);
        
        physx::PxRaycastBuffer hitBuffer;
        physx::PxQueryFilterData filterData;
        filterData.flags = physx::PxQueryFlag::eSTATIC;
        
        dynAttach.isGrounded = pxScene->raycast(
            actorPos, rayDir, dynAttach.groundCheckDistance,
            hitBuffer, physx::PxHitFlag::eDEFAULT, filterData
        );
        
        // 2. 检测速度
        physx::PxVec3 vel = dynamicActor->getLinearVelocity();
        float speed = vel.magnitude();
        bool isStable = speed < dynAttach.velocityThreshold;
        
        // 3. 更新稳定计时器
        if (dynAttach.isGrounded && isStable) {
            dynAttach.currentStableTime += deltaTime;
            
            // 4. 检查是否应该切换到 GROUNDED
            if (dynAttach.currentStableTime >= dynAttach.groundedStableTime) {
                SwitchToGrounded(entity, dynAttach, rigidBody, gravity, attached, 
                                dynamicActor, registry);
                
                std::cout << "[DynAttach] Entity switched to GROUNDED state" << std::endl;
            }
        } else {
            dynAttach.currentStableTime = 0.0f;
        }
        
        if (shouldDebug) {
            std::cout << "[DynAttach-Dynamic] Grounded=" << (dynAttach.isGrounded ? "YES" : "NO")
                     << ", Speed=" << speed << " m/s"
                     << ", StableTime=" << dynAttach.currentStableTime << "s" << std::endl;
        }
    }
    
    /**
     * GROUNDED 状态更新：检测是否应该唤醒
     */
    void UpdateGroundedState(float deltaTime, Entity entity,
                            DynamicAttachComponent& dynAttach,
                            RigidBodyComponent& rigidBody,
                            GravityAffectedComponent& gravity,
                            AttachedToReferenceFrameComponent& attached,
                            physx::PxRigidDynamic* dynamicActor,
                            entt::registry& registry,
                            bool shouldDebug) {
        using namespace DirectX;
        using namespace components;
        
        // 检查是否需要唤醒
        bool shouldWakeUp = dynAttach.wakeUpRequested;
        
        // 也可以检测速度（如果被外力推动）
        physx::PxVec3 vel = dynamicActor->getLinearVelocity();
        float speed = vel.magnitude();
        if (speed > dynAttach.wakeUpVelocityThreshold) {
            shouldWakeUp = true;
        }
        
        if (shouldWakeUp) {
            SwitchToDynamic(entity, dynAttach, rigidBody, attached, dynamicActor, registry);
            dynAttach.wakeUpRequested = false;
            
            std::cout << "[DynAttach] Entity woke up to DYNAMIC state" << std::endl;
            return;
        }
        
        // GROUNDED 状态：使用静态吸附（由 ReferenceFrameSystem 处理）
        // 这里只需要确保 staticAttach = true
        
        if (shouldDebug) {
            std::cout << "[DynAttach-Grounded] Static attach active" << std::endl;
        }
    }
    
    /**
     * 切换到 GROUNDED 状态
     */
    void SwitchToGrounded(Entity entity,
                         DynamicAttachComponent& dynAttach,
                         RigidBodyComponent& rigidBody,
                         GravityAffectedComponent& gravity,
                         AttachedToReferenceFrameComponent& attached,
                         physx::PxRigidDynamic* dynamicActor,
                         entt::registry& registry) {
        using namespace DirectX;
        using namespace components;
        
        dynAttach.currentState = DynamicAttachComponent::State::GROUNDED;
        dynAttach.currentStableTime = 0.0f;
        
        // 记录当前位置作为本地坐标
        if (attached.referenceFrame != entt::null) {
            auto* refTransform = registry.try_get<TransformComponent>(attached.referenceFrame);
            if (refTransform) {
                physx::PxVec3 pos = dynamicActor->getGlobalPose().p;
                physx::PxQuat rot = dynamicActor->getGlobalPose().q;
                
                XMVECTOR refPos = XMLoadFloat3(&refTransform->position);
                XMVECTOR objPos = XMVectorSet(pos.x, pos.y, pos.z, 0.0f);
                XMVECTOR localOffset = XMVectorSubtract(objPos, refPos);
                
                XMStoreFloat3(&dynAttach.groundedLocalOffset, localOffset);
                dynAttach.groundedRotation = XMFLOAT4(rot.x, rot.y, rot.z, rot.w);
                dynAttach.groundedInitialized = true;
                
                // 同步到 attached 组件
                attached.localOffset = dynAttach.groundedLocalOffset;
                attached.isInitialized = true;
            }
        }
        
        // 启用静态吸附模式
        attached.staticAttach = true;
        
        // 清零速度
        dynamicActor->setLinearVelocity(physx::PxVec3(0, 0, 0));
        dynamicActor->setAngularVelocity(physx::PxVec3(0, 0, 0));
        
        // 设置为 Kinematic（可选，更稳定）
        dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
        rigidBody.isKinematic = true;
    }
    
    /**
     * 切换到 DYNAMIC 状态
     */
    void SwitchToDynamic(Entity entity,
                        DynamicAttachComponent& dynAttach,
                        RigidBodyComponent& rigidBody,
                        AttachedToReferenceFrameComponent& attached,
                        physx::PxRigidDynamic* dynamicActor,
                        entt::registry& registry) {
        
        dynAttach.currentState = DynamicAttachComponent::State::DYNAMIC;
        dynAttach.groundedInitialized = false;
        
        // 禁用静态吸附模式
        attached.staticAttach = false;
        
        // 恢复为 Dynamic
        dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, false);
        rigidBody.isKinematic = false;
        
        // 唤醒 Actor
        dynamicActor->wakeUp();
    }
};

} // namespace outer_wilds
