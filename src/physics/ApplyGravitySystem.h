#pragma once
#include "../core/ECS.h"
#include "components/GravityAffectedComponent.h"
#include "components/RigidBodyComponent.h"
#include "../scene/components/TransformComponent.h"
#include "../gameplay/components/SpacecraftComponent.h"
#include "PhysXManager.h"
#include <DirectXMath.h>
#include <entt/entt.hpp>
#include <PxPhysicsAPI.h>

using namespace DirectX;
using namespace outer_wilds;
using namespace outer_wilds::components;

namespace outer_wilds {

/**
 * 应用重力系统 - 简化版
 * 
 * 设计原则：
 * 1. 只负责施加重力力到 PhysX 动态刚体
 * 2. 不干预 PhysX 的碰撞检测和响应
 * 3. 让 PhysX 自己处理接地、碰撞、弹跳等
 * 4. 重力方向由 GravitySystem 计算（指向最近星球中心）
 * 
 * 职责:
 *  - 读取 GravityAffectedComponent 的重力方向和强度
 *  - 对每个动态刚体施加重力力 F = m * g
 */
class ApplyGravitySystem : public System {
public:
    void Update(float deltaTime, entt::registry& registry) override {
        // 遍历所有同时具有重力影响和刚体的实体
        auto view = registry.view<GravityAffectedComponent, RigidBodyComponent>();
        
        for (auto entity : view) {
            auto& gravity = view.get<GravityAffectedComponent>(entity);
            auto& rigidBody = view.get<RigidBodyComponent>(entity);
            
            // 只处理受重力影响的实体
            if (!gravity.affectedByGravity) {
                continue;
            }
            
            // 只处理动态刚体
            if (!rigidBody.physxActor) {
                continue;
            }
            
            // 跳过 Kinematic 物体（不能对其施加力）
            if (rigidBody.isKinematic) {
                continue;
            }
            
            // 转换为 PhysX 动态 actor
            physx::PxRigidDynamic* dynamicActor = rigidBody.physxActor->is<physx::PxRigidDynamic>();
            if (!dynamicActor) {
                continue;
            }
            
            // 检查 PhysX 层面是否为 Kinematic
            if (dynamicActor->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC) {
                continue;
            }
            
            // === 计算重力 ===
            float gravityStrength = gravity.currentGravityStrength * gravity.gravityScale;
            
            // 重力力 = 质量 * 重力加速度 * 重力方向
            XMVECTOR gravityDir = XMLoadFloat3(&gravity.currentGravityDir);
            XMVECTOR gravityForce = XMVectorScale(gravityDir, rigidBody.mass * gravityStrength);
            
            XMFLOAT3 forceF;
            XMStoreFloat3(&forceF, gravityForce);
            
            // 施加重力力
            dynamicActor->addForce(
                physx::PxVec3(forceF.x, forceF.y, forceF.z), 
                physx::PxForceMode::eFORCE
            );
            
            // === 设置阻尼 ===
            // 检查是否为飞船，飞船使用较低阻尼
            auto* spacecraftComp = registry.try_get<SpacecraftComponent>(entity);
            if (spacecraftComp) {
                // 飞船：低阻尼，太空中自由飞行
                dynamicActor->setLinearDamping(0.1f);
                dynamicActor->setAngularDamping(0.5f);
            } else {
                // 其他物体：使用配置的阻尼
                dynamicActor->setLinearDamping(rigidBody.drag);
                dynamicActor->setAngularDamping(rigidBody.angularDrag);
            }
        }
    }
};

} // namespace outer_wilds
