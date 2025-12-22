#pragma once
#include "../core/ECS.h"
#include "components/StandingOnComponent.h"
#include "../physics/components/RigidBodyComponent.h"
#include "../physics/components/GravitySourceComponent.h"
#include "../scene/components/TransformComponent.h"
#include "../physics/components/GravityAffectedComponent.h"
#include <DirectXMath.h>
#include <PxPhysicsAPI.h>

namespace outer_wilds {

/**
 * 刚体天体跟随系统 - 让RigidBody物体跟随移动的天体（如轨道运动的行星）
 * 
 * 工作原理:
 * 1. 使用球体距离检测判断物体是否在天体表面
 * 2. 计算天体的位移和速度
 * 3. 直接将天体的线性速度应用到刚体上（让它跟随天体运动）
 * 4. 完全清零相对于天体的运动（防止滑动）
 * 
 * 与CelestialFollowSystem的区别:
 * - CelestialFollowSystem: 用于CharacterController（玩家）
 * - RigidBodyCelestialFollowSystem: 用于RigidDynamic（飞船、箱子等物理物体）
 * 
 * 更新顺序: 必须在PhysicsSystem同步Transform之后运行
 */
class RigidBodyCelestialFollowSystem : public System {
public:
    void Update(float deltaTime, entt::registry& registry) override {
        using namespace DirectX;
        using namespace components;
        
        // 查找所有具有StandingOn、RigidBody和GravityAffected组件的实体
        auto view = registry.view<StandingOnComponent, RigidBodyComponent, GravityAffectedComponent, TransformComponent>();
        
        for (auto entity : view) {
            auto& standing = view.get<StandingOnComponent>(entity);
            auto& rigidBody = view.get<RigidBodyComponent>(entity);
            auto& gravity = view.get<GravityAffectedComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);
            
            // 检查是否启用跟随且有有效的PhysX Actor
            if (!standing.followEnabled || !rigidBody.physxActor) continue;
            
            // 跳过 Kinematic 物体（它们由其他系统管理位置）
            if (rigidBody.isKinematic) continue;
            
            // 确保是动态刚体（只有动态刚体可以被移动）
            physx::PxRigidDynamic* dynamicActor = rigidBody.physxActor->is<physx::PxRigidDynamic>();
            if (!dynamicActor) continue;
            
            // === 步骤1: 使用距离检测确定当前站在哪个天体上 ===
            Entity currentCelestial = DetermineStandingEntityByDistance(registry, gravity, transform);
            
            // === 步骤2: 如果站在天体上，匹配天体的运动速度 ===
            if (currentCelestial != entt::null) {
                auto* celestialTransform = registry.try_get<TransformComponent>(currentCelestial);
                if (celestialTransform) {
                    // 检测天体切换
                    if (currentCelestial != standing.standingOnEntity) {
                        standing.previousStandingEntity = standing.standingOnEntity;
                        standing.standingOnEntity = currentCelestial;
                        standing.relativePosition = celestialTransform->position;
                    }
                    
                    // 计算天体的位移（与玩家系统一样：直接移动，不设置速度）
                    XMVECTOR lastPos = XMLoadFloat3(&standing.relativePosition);
                    XMVECTOR currentPos = XMLoadFloat3(&celestialTransform->position);
                    XMVECTOR displacement = XMVectorSubtract(currentPos, lastPos);
                    
                    float displacementMagnitude = XMVectorGetX(XMVector3Length(displacement));
                    if (displacementMagnitude > 0.0001f) {
                        // 应用位移到刚体（直接传送，像玩家一样）
                        ApplyCelestialDisplacement(dynamicActor, displacement);
                        
                        // 更新记录的天体位置
                        standing.relativePosition = celestialTransform->position;
                    }
                }
            } else {
                // 不在任何天体上，清空记录
                standing.standingOnEntity = entt::null;
       
    Entity DetermineStandingEntityByDistance(
        entt::registry& registry,
        const GravityAffectedComponent& gravity,
        const TransformComponent& objectTransform
    ) {
        using namespace DirectX;
        
        // 如果没有重力源，不在任何天体上
        if (gravity.currentGravitySource == entt::null) {
            return entt::null;
        }
        
        // 获取天体的Transform和重力源组件
        auto* celestialTransform = registry.try_get<TransformComponent>(gravity.currentGravitySource);
        auto* gravitySource = registry.try_get<components::GravitySourceComponent>(gravity.currentGravitySource);
        
        if (!celestialTransform || !gravitySource) {
            return entt::null;
        }
        
        // 计算物体到天体中心的距离
        XMVECTOR objectPos = XMLoadFloat3(&objectTransform.position);
        XMVECTOR celestialPos = XMLoadFloat3(&celestialTransform->position);
        XMVECTOR toObject = XMVectorSubtract(objectPos, celestialPos);
        float distance = XMVectorGetX(XMVector3Length(toObject));
        
        // 计算天体表面高度（半径 + 检测阈值）
        float surfaceDistance = gravitySource->radius;
        float detectionThreshold = 5.0f; // 5米内认为在表面上
        
        // 如果在表面附近，认为在天体上
        if (distance <= surfaceDistance + detectionThreshold) {
            return gravity.currentGravitySource;
        }
        
        return entt::null;
    }
    
    /**
     * 将天体的位移应用到刚体（直接传送，与玩家系统一致）
     */
    void ApplyCelestialDisplacement(
        physx::PxRigidDynamic* actor,
        DirectX::XMVECTOR displacement
    ) {
        using namespace DirectX;
        
        // 获取刚体当前位置
        physx::PxTransform currentTransform = actor->getGlobalPose();
        physx::PxVec3 currentPos = currentTransform.p;
        
        // 应用位移
        XMVECTOR actorPos = XMVectorSet(currentPos.x, currentPos.y, currentPos.z, 0.0f);
        XMVECTOR newPos = XMVectorAdd(actorPos, displacement);
        
        // 设置新位置（直接传送）
        XMFLOAT3 newPosF;
        XMStoreFloat3(&newPosF, newPos);
        currentTransform.p = physx::PxVec3(newPosF.x, newPosF.y, newPosF.z);
        actor->setGlobalPose(currentTransform);
        
        // 清零速度，让刚体相对天体静止
        actor->setLinearVelocity(physx::PxVec3(0.0f, 0.0f, 0.0f));
        actor->setAngularVelocity(physx::PxVec3(0.0f, 0.0f, 0.0f));
    }
};

} // namespace outer_wilds
