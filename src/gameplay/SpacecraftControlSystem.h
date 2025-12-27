#pragma once
#include <entt/entt.hpp>
#include <DirectXMath.h>
#include <PxPhysicsAPI.h>
#include <iostream>
#include <algorithm>
#include "../core/ECS.h"
#include "components/SpacecraftComponent.h"
#include "../scene/components/TransformComponent.h"
#include "../physics/components/RigidBodyComponent.h"
#include "../physics/components/SectorComponent.h"
#include "../input/InputManager.h"

namespace outer_wilds::systems {

using namespace components;
using namespace DirectX;

/**
 * 飞船控制系统 - 真正的PhysX物理模拟
 * 
 * 设计原则：
 * 1. 所有运动通过 PhysX 力/力矩实现
 * 2. 不直接修改 Transform，让物理引擎驱动
 * 3. 基于 Sector 系统的局部坐标工作
 * 4. 六向控制：WASD + Shift/Ctrl + QE + 方向键
 */
class SpacecraftControlSystem : public System {
public:
    void Update(float deltaTime, entt::registry& registry) override {
        auto view = registry.view<SpacecraftComponent, TransformComponent, RigidBodyComponent>();
        
        for (auto entity : view) {
            auto& spacecraft = view.get<SpacecraftComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);
            auto& rigidBody = view.get<RigidBodyComponent>(entity);
            
            // 只处理正在驾驶的飞船
            if (spacecraft.currentState != SpacecraftComponent::State::PILOTED) {
                continue;
            }
            
            // 需要有效的 PhysX actor
            if (!rigidBody.physxActor) {
                continue;
            }
            
            physx::PxRigidDynamic* dynamicActor = rigidBody.physxActor->is<physx::PxRigidDynamic>();
            if (!dynamicActor) {
                continue;
            }
            
            // 收集输入
            CollectInput(spacecraft);
            
            // 应用物理控制（通过力和力矩）
            ApplyPhysicsControl(spacecraft, transform, dynamicActor, deltaTime);
            
            // 更新状态信息
            UpdateStatusInfo(spacecraft, dynamicActor);
            
            // 调试输出（每2秒一次）
            static float debugTimer = 0.0f;
            debugTimer += deltaTime;
            if (debugTimer >= 2.0f) {
                debugTimer = 0.0f;
                
                physx::PxVec3 vel = dynamicActor->getLinearVelocity();
                physx::PxVec3 angVel = dynamicActor->getAngularVelocity();
                physx::PxTransform pose = dynamicActor->getGlobalPose();
                
                std::cout << "[Spacecraft] === PhysX Debug ===" << std::endl;
                std::cout << "  Position: (" << pose.p.x << ", " << pose.p.y << ", " << pose.p.z << ")" << std::endl;
                std::cout << "  Velocity: (" << vel.x << ", " << vel.y << ", " << vel.z 
                          << ") Speed: " << vel.magnitude() << " m/s" << std::endl;
                std::cout << "  AngVel: (" << angVel.x << ", " << angVel.y << ", " << angVel.z << ") rad/s" << std::endl;
                std::cout << "  Input: F=" << spacecraft.inputForward << " S=" << spacecraft.inputStrafe 
                          << " V=" << spacecraft.inputVertical << " R=" << spacecraft.inputRoll 
                          << " P=" << spacecraft.inputPitch << " Y=" << spacecraft.inputYaw << std::endl;
            }
        }
    }

private:
    /**
     * 收集键盘输入
     */
    void CollectInput(SpacecraftComponent& spacecraft) {
        auto& input = InputManager::GetInstance();
        
        // 重置输入
        spacecraft.inputForward = 0.0f;
        spacecraft.inputStrafe = 0.0f;
        spacecraft.inputVertical = 0.0f;
        spacecraft.inputRoll = 0.0f;
        spacecraft.inputPitch = 0.0f;
        spacecraft.inputYaw = 0.0f;
        
        // 前后 (W/S)
        if (input.IsKeyHeld('W')) spacecraft.inputForward = 1.0f;
        if (input.IsKeyHeld('S')) spacecraft.inputForward = -1.0f;
        
        // 左右 (A/D)
        if (input.IsKeyHeld('A')) spacecraft.inputStrafe = -1.0f;
        if (input.IsKeyHeld('D')) spacecraft.inputStrafe = 1.0f;
        
        // 上下 (Shift/Ctrl)
        if (input.IsKeyHeld(VK_SHIFT)) spacecraft.inputVertical = 1.0f;
        if (input.IsKeyHeld(VK_CONTROL)) spacecraft.inputVertical = -1.0f;
        
        // 滚转 (Q/E)
        if (input.IsKeyHeld('Q')) spacecraft.inputRoll = -1.0f;
        if (input.IsKeyHeld('E')) spacecraft.inputRoll = 1.0f;
        
        // 俯仰 (上/下箭头)
        if (input.IsKeyHeld(VK_UP)) spacecraft.inputPitch = 1.0f;
        if (input.IsKeyHeld(VK_DOWN)) spacecraft.inputPitch = -1.0f;
        
        // 偏航 (左/右箭头)
        if (input.IsKeyHeld(VK_LEFT)) spacecraft.inputYaw = -1.0f;
        if (input.IsKeyHeld(VK_RIGHT)) spacecraft.inputYaw = 1.0f;
    }
    
    /**
     * 通过PhysX力和力矩控制飞船
     */
    void ApplyPhysicsControl(SpacecraftComponent& spacecraft, 
                             TransformComponent& transform,
                             physx::PxRigidDynamic* dynamicActor,
                             float deltaTime) {
        // 获取飞船当前姿态
        physx::PxTransform pose = dynamicActor->getGlobalPose();
        XMVECTOR rotQuat = XMVectorSet(pose.q.x, pose.q.y, pose.q.z, pose.q.w);
        
        // 飞船模型被绕X轴旋转了-90度，所以坐标轴变换为：
        // - 原来的Z轴（前）变成了-Y轴
        // - 原来的Y轴（上）变成了Z轴
        // - X轴保持不变
        XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, -1, 0, 0), rotQuat);  // 前（-Y）
        XMVECTOR right = XMVector3Rotate(XMVectorSet(1, 0, 0, 0), rotQuat);     // 右（X）
        XMVECTOR up = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), rotQuat);        // 上（Z）
        
        // === 计算推力 ===
        XMVECTOR totalForce = XMVectorZero();
        
        if (spacecraft.inputForward != 0.0f) {
            totalForce = XMVectorAdd(totalForce, 
                XMVectorScale(forward, spacecraft.inputForward * spacecraft.mainThrust));
        }
        if (spacecraft.inputStrafe != 0.0f) {
            totalForce = XMVectorAdd(totalForce, 
                XMVectorScale(right, spacecraft.inputStrafe * spacecraft.strafeThrust));
        }
        if (spacecraft.inputVertical != 0.0f) {
            totalForce = XMVectorAdd(totalForce, 
                XMVectorScale(up, spacecraft.inputVertical * spacecraft.verticalThrust));
        }
        
        // 应用推力
        XMFLOAT3 forceF;
        XMStoreFloat3(&forceF, totalForce);
        dynamicActor->addForce(physx::PxVec3(forceF.x, forceF.y, forceF.z), physx::PxForceMode::eFORCE);
        
        // === 计算力矩 ===
        // 局部坐标系下的力矩
        XMFLOAT3 localTorque = { 0.0f, 0.0f, 0.0f };
        
        // 俯仰（绕局部X轴，即右向轴）
        if (spacecraft.inputPitch != 0.0f) {
            localTorque.x = spacecraft.inputPitch * spacecraft.pitchTorque;
        }
        
        // 偏航（绕局部Z轴，对于旋转后的模型是上向轴）
        if (spacecraft.inputYaw != 0.0f) {
            localTorque.z = spacecraft.inputYaw * spacecraft.yawTorque;
        }
        
        // 滚转（绕局部-Y轴，对于旋转后的模型是前向轴）
        if (spacecraft.inputRoll != 0.0f) {
            localTorque.y = -spacecraft.inputRoll * spacecraft.rollTorque;
        }
        
        // 将局部力矩转换为世界坐标系
        XMVECTOR localTorqueVec = XMLoadFloat3(&localTorque);
        XMVECTOR worldTorque = XMVector3Rotate(localTorqueVec, rotQuat);
        
        XMFLOAT3 torqueF;
        XMStoreFloat3(&torqueF, worldTorque);
        dynamicActor->addTorque(physx::PxVec3(torqueF.x, torqueF.y, torqueF.z), physx::PxForceMode::eFORCE);
    }
    
    /**
     * 更新飞船状态信息
     */
    void UpdateStatusInfo(SpacecraftComponent& spacecraft, physx::PxRigidDynamic* dynamicActor) {
        physx::PxVec3 vel = dynamicActor->getLinearVelocity();
        spacecraft.currentVelocity = { vel.x, vel.y, vel.z };
        spacecraft.currentSpeed = vel.magnitude();
        
        physx::PxVec3 angVel = dynamicActor->getAngularVelocity();
        spacecraft.currentAngularVelocity = { angVel.x, angVel.y, angVel.z };
    }
};

} // namespace outer_wilds::systems
