#pragma once
#include "../core/ECS.h"
#include "components/GravitySourceComponent.h"
#include "components/GravityAffectedComponent.h"
#include "components/RigidBodyComponent.h"
#include "../gameplay/components/StandingOnComponent.h"
#include "../scene/components/TransformComponent.h"
#include <DirectXMath.h>
#include <PxPhysicsAPI.h>
#include <entt/entt.hpp>

using namespace DirectX;
using namespace outer_wilds;
using namespace outer_wilds::components;

namespace outer_wilds {

/**
 * 天体跟随系统（刚体） - 让刚体物体跟随移动的天体
 * 
 * 职责:
 *  1. 使用球体距离检测判断刚体是否在天体表面
 *  2. 计算天体的运动速度
 *  3. 将天体的速度直接应用到刚体上，使其相对静止
 *  4. 防止刚体在天体表面滑动
 * 
 * 适用对象: 飞船、箱子等使用RigidBody的动态物体
 * 更新顺序: 在PhysicsSystem之后运行
 */
class CelestialBodyFollowSystem : public System {
public:
    void Update(float deltaTime, entt::registry& registry) override {
        // 遍历所有需要跟随天体的刚体
        auto view = registry.view<
            StandingOnComponent,
            RigidBodyComponent,
            GravityAffectedComponent,
            TransformComponent
        >();
        
        for (auto entity : view) {
            auto& standing = view.get<StandingOnComponent>(entity);
            auto& rigidBody = view.get<RigidBodyComponent>(entity);
            auto& gravity = view.get<GravityAffectedComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);
            
            // 检查是否启用跟随且有有效的PhysX Actor
            if (!standing.followEnabled || !rigidBody.physxActor) continue;
            
            // 跳过 Kinematic 物体（它们由其他系统管理位置）
            if (rigidBody.isKinematic) continue;
            
            // 确保是动态刚体
            physx::PxRigidDynamic* dynamicActor = rigidBody.physxActor->is<physx::PxRigidDynamic>();
            if (!dynamicActor) continue;
            
            // 步骤1: 使用距离检测判断是否在天体表面
            Entity currentCelestial = DetermineStandingCelestial(
                registry, gravity, transform);
            
            // 步骤2: 如果在天体上，应用天体速度
            if (currentCelestial != entt::null) {
                ApplyCelestialMotion(registry, standing, dynamicActor,
                    currentCelestial, deltaTime);
            } else {
                // 不在任何天体上，清空记录
                standing.standingOnEntity = entt::null;
            }
        }
    }

private:
    /**
     * 使用球体距离检测判断实体是否在天体表面
     */
    Entity DetermineStandingCelestial(
        entt::registry& registry,
        const GravityAffectedComponent& gravity,
        const TransformComponent& objectTransform
    ) {
        // 如果没有重力源，不在任何天体上
        if (gravity.currentGravitySource == entt::null) {
            return entt::null;
        }
        
        // 获取天体的Transform和重力源组件
        auto* celestialTransform = registry.try_get<TransformComponent>(
            gravity.currentGravitySource);
        auto* gravitySource = registry.try_get<GravitySourceComponent>(
            gravity.currentGravitySource);
        
        if (!celestialTransform || !gravitySource) {
            return entt::null;
        }
        
        // 计算物体到天体中心的距离
        XMVECTOR objectPos = XMLoadFloat3(&objectTransform.position);
        XMVECTOR celestialPos = XMLoadFloat3(&celestialTransform->position);
        XMVECTOR toObject = XMVectorSubtract(objectPos, celestialPos);
        float distance = XMVectorGetX(XMVector3Length(toObject));
        
        // 表面检测阈值（半径 + 5米缓冲区）
        float surfaceThreshold = gravitySource->radius + 5.0f;
        
        // 在表面附近则认为站在天体上
        if (distance <= surfaceThreshold) {
            return gravity.currentGravitySource;
        }
        
        return entt::null;
    }
    
    /**
     * 应用天体运动到刚体
     */
    void ApplyCelestialMotion(
        entt::registry& registry,
        StandingOnComponent& standing,
        physx::PxRigidDynamic* actor,
        Entity celestialEntity,
        float deltaTime
    ) {
        auto* celestialTransform = registry.try_get<TransformComponent>(celestialEntity);
        if (!celestialTransform) return;
        
        // 检测天体切换
        if (celestialEntity != standing.standingOnEntity) {
            standing.previousStandingEntity = standing.standingOnEntity;
            standing.standingOnEntity = celestialEntity;
            standing.relativePosition = celestialTransform->position;
        }
        
        // 计算天体速度（位移/时间）
        XMVECTOR lastPos = XMLoadFloat3(&standing.relativePosition);
        XMVECTOR currentPos = XMLoadFloat3(&celestialTransform->position);
        XMVECTOR displacement = XMVectorSubtract(currentPos, lastPos);
        
        float displacementMagnitude = XMVectorGetX(XMVector3Length(displacement));
        
        if (displacementMagnitude > 0.0001f && deltaTime > 0.0001f) {
            // 计算天体线性速度
            XMVECTOR celestialVelocity = XMVectorScale(displacement, 1.0f / deltaTime);
            
            // 转换为PhysX格式
            XMFLOAT3 velocityF;
            XMStoreFloat3(&velocityF, celestialVelocity);
            physx::PxVec3 pxVelocity(velocityF.x, velocityF.y, velocityF.z);
            
            // 直接设置刚体速度与天体一致
            actor->setLinearVelocity(pxVelocity);
            actor->setAngularVelocity(physx::PxVec3(0.0f, 0.0f, 0.0f));
            
            // 更新记录的位置
            standing.relativePosition = celestialTransform->position;
        } else {
            // 天体静止，确保刚体也静止
            actor->setLinearVelocity(physx::PxVec3(0.0f, 0.0f, 0.0f));
            actor->setAngularVelocity(physx::PxVec3(0.0f, 0.0f, 0.0f));
        }
    }
};

} // namespace outer_wilds
