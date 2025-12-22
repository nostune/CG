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
 * 应用重力系统 - 将GravitySystem计算的重力应用到PhysX刚体
 * 
 * 核心策略：接地时直接归零速度（类似CharacterController的做法）
 * 
 * 职责:
 *  1. 读取 GravityAffectedComponent 的重力方向和强度
 *  2. 使用射线检测判断刚体是否接地
 *  3. 接地时归零所有速度，只保留必要的贴地力
 *  4. 未接地时正常应用重力
 */
class ApplyGravitySystem : public System {
public:
    void Update(float deltaTime, entt::registry& registry) override {
        auto* pxScene = PhysXManager::GetInstance().GetScene();
        if (!pxScene) return;
        
        // 遍历所有同时具有重力影响和刚体的实体
        auto view = registry.view<GravityAffectedComponent, RigidBodyComponent, TransformComponent>();
        
        for (auto entity : view) {
            auto& gravity = view.get<GravityAffectedComponent>(entity);
            auto& rigidBody = view.get<RigidBodyComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);
            
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
            
            // 转换为PhysX动态actor
            physx::PxRigidDynamic* dynamicActor = rigidBody.physxActor->is<physx::PxRigidDynamic>();
            if (!dynamicActor) {
                continue;  // 不是动态actor
            }
            
            // 再次检查 PhysX 层面是否为 Kinematic
            if (dynamicActor->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC) {
                continue;
            }
            
            // 1. 获取重力方向和强度
            XMVECTOR gravityDir = XMLoadFloat3(&gravity.currentGravityDir);
            float gravityStrength = gravity.currentGravityStrength * gravity.gravityScale;
            
            // 2. 使用射线检测判断是否接地
            physx::PxVec3 actorPos = dynamicActor->getGlobalPose().p;
            XMFLOAT3 gravDirF = gravity.currentGravityDir;
            physx::PxVec3 rayDir(gravDirF.x, gravDirF.y, gravDirF.z);
            
            // 射线从刚体中心沿重力方向发射
            // 使用较短的距离来精确检测接地
            const float GROUND_CHECK_DISTANCE = 0.8f;  // 略大于胶囊体半高
            physx::PxRaycastBuffer hitBuffer;
            
            // 设置查询过滤器，只检测静态物体（地面）
            physx::PxQueryFilterData filterData;
            filterData.flags = physx::PxQueryFlag::eSTATIC;
            
            bool isGrounded = pxScene->raycast(
                actorPos, 
                rayDir, 
                GROUND_CHECK_DISTANCE,
                hitBuffer,
                physx::PxHitFlag::eDEFAULT,
                filterData
            );
            
            // 3. 获取当前速度
            physx::PxVec3 linearVel = dynamicActor->getLinearVelocity();
            float speed = linearVel.magnitude();
            
            // 沿重力方向的速度分量（正值=朝重力方向，负值=离开地面）
            XMVECTOR velVec = XMVectorSet(linearVel.x, linearVel.y, linearVel.z, 0.0f);
            float gravityDirSpeed = XMVectorGetX(XMVector3Dot(velVec, gravityDir));
            
            // 调试输出
            static int debugFrame = 0;
            bool shouldDebug = (++debugFrame % 60 == 0); // 每秒输出一次
            
            // 4. 根据接地状态处理
            if (isGrounded) {
                // === 接地状态 ===
                // 关键：直接归零速度，类似CharacterController的做法
                // 不再尝试衰减，直接设为0
                
                bool velocityZeroed = false;
                if (speed > 0.05f) {
                    // 如果还在移动，强制停止
                    dynamicActor->setLinearVelocity(physx::PxVec3(0, 0, 0));
                    dynamicActor->setAngularVelocity(physx::PxVec3(0, 0, 0));
                    velocityZeroed = true;
                }
                
                if (shouldDebug) {
                    std::cout << "[ApplyGravity] Grounded=YES, HitDist=" << hitBuffer.block.distance
                             << ", Speed=" << speed << " m/s"
                             << ", VelocityZeroed=" << (velocityZeroed ? "YES" : "NO") << std::endl;
                }
                
                // 施加很小的贴地力（只需要防止弹起）
                XMVECTOR stickForce = XMVectorScale(gravityDir, rigidBody.mass * 1.0f);
                XMFLOAT3 stickForceF;
                XMStoreFloat3(&stickForceF, stickForce);
                dynamicActor->addForce(physx::PxVec3(stickForceF.x, stickForceF.y, stickForceF.z), 
                                       physx::PxForceMode::eFORCE);
                
            } else {
                if (shouldDebug) {
                    std::cout << "[ApplyGravity] Grounded=NO, Speed=" << speed << " m/s" << std::endl;
                }
                // === 空中状态 ===
                // 正常施加重力
                XMVECTOR gravityAccel = XMVectorScale(gravityDir, gravityStrength);
                XMVECTOR gravityForce = XMVectorScale(gravityAccel, rigidBody.mass);
                
                XMFLOAT3 gravityForceF;
                XMStoreFloat3(&gravityForceF, gravityForce);
                
                dynamicActor->addForce(
                    physx::PxVec3(gravityForceF.x, gravityForceF.y, gravityForceF.z), 
                    physx::PxForceMode::eFORCE
                );
            }
            
            // 5. 应用阻尼（保持较高值以增加稳定性）
            dynamicActor->setLinearDamping(rigidBody.drag);
            dynamicActor->setAngularDamping(rigidBody.angularDrag);
        }
    }
};

} // namespace outer_wilds
