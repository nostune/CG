/**
 * SpacecraftDrivingSystem.cpp
 * 
 * 飞船驾驶系统实现
 * 
 * 控制方案：
 * - W/S: 前后推进
 * - A/D: 左右平移
 * - Shift/Ctrl: 上下推进
 * - Q/E: 滚转
 * - ↑/↓: 俯仰
 * - ←/→: 偏航
 */

#include "SpacecraftDrivingSystem.h"
#include "components/SpacecraftComponent.h"
#include "../scene/components/TransformComponent.h"
#include "../physics/components/RigidBodyComponent.h"
#include "../physics/components/SectorComponent.h"
#include "../graphics/components/CameraComponent.h"
#include "../graphics/components/FreeCameraComponent.h"
#include "../input/InputManager.h"
#include "../core/DebugManager.h"
#include <Windows.h>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace outer_wilds {

using namespace components;
using namespace DirectX;

void SpacecraftDrivingSystem::Initialize() {
    DebugManager::GetInstance().Log("SpacecraftDrivingSystem", "Initialized - 6DOF physics-based spacecraft control");
}

void SpacecraftDrivingSystem::Initialize(std::shared_ptr<Scene> scene) {
    m_Scene = scene;
    Initialize();
}

void SpacecraftDrivingSystem::Update(float deltaTime, entt::registry& registry) {
    ProcessSpacecraftInput(registry);
    ApplySpacecraftForces(deltaTime, registry);
    UpdateSpacecraftState(registry);
    UpdateSpacecraftCamera(deltaTime, registry);
}

void SpacecraftDrivingSystem::Shutdown() {
    DebugManager::GetInstance().Log("SpacecraftDrivingSystem", "Shutdown");
}

void SpacecraftDrivingSystem::ProcessSpacecraftInput(entt::registry& registry) {
    auto view = registry.view<SpacecraftComponent>();
    
    for (auto entity : view) {
        auto& spacecraft = view.get<SpacecraftComponent>(entity);
        
        // 只处理 PILOTED 状态的飞船
        if (spacecraft.currentState != SpacecraftComponent::State::PILOTED) {
            // IDLE 状态清空输入
            spacecraft.inputForward = 0.0f;
            spacecraft.inputStrafe = 0.0f;
            spacecraft.inputVertical = 0.0f;
            spacecraft.inputRoll = 0.0f;
            spacecraft.inputPitch = 0.0f;
            spacecraft.inputYaw = 0.0f;
            continue;
        }
        
        // === 平移控制（键盘）===
        // W/S - 前后
        float forward = 0.0f;
        if (GetAsyncKeyState('W') & 0x8000) forward += 1.0f;
        if (GetAsyncKeyState('S') & 0x8000) forward -= 1.0f;
        
        // A/D - 左右
        float strafe = 0.0f;
        if (GetAsyncKeyState('D') & 0x8000) strafe += 1.0f;
        if (GetAsyncKeyState('A') & 0x8000) strafe -= 1.0f;
        
        // Shift/Ctrl - 上下
        float vertical = 0.0f;
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) vertical += 1.0f;
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) vertical -= 1.0f;
        
        // === 旋转控制：键盘 + 鼠标 ===
        // 方向键左右 - 滚转（绕前向轴旋转）
        float roll = 0.0f;
        if (GetAsyncKeyState(VK_LEFT) & 0x8000) roll += 1.0f;
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) roll -= 1.0f;
        
        // 方向键上下 - 俯仰
        float pitch = 0.0f;
        if (GetAsyncKeyState(VK_UP) & 0x8000) pitch += 1.0f;
        if (GetAsyncKeyState(VK_DOWN) & 0x8000) pitch -= 1.0f;
        
        // Q/E - 偏航（摆动，绕上轴旋转）
        float yaw = 0.0f;
        if (GetAsyncKeyState('Q') & 0x8000) yaw -= 1.0f;
        if (GetAsyncKeyState('E') & 0x8000) yaw += 1.0f;

        // 鼠标控制俯仰/偏航（仅在开启鼠标视角时生效）
        const auto& playerInput = InputManager::GetInstance().GetPlayerInput();
        if (playerInput.mouseLookEnabled) {
            // 基础缩放：先把像素位移压到一个统一区间
            const float mouseScale = 1.0f / 320.0f;
            float mx = playerInput.lookInput.x * mouseScale;
            float my = playerInput.lookInput.y * mouseScale;

            // 径向死区：对 (mx, my) 的长度做死区处理，斜向移动更平滑
            float len = std::sqrt(mx * mx + my * my);
            const float deadZone = 0.02f; // 更小的阈值，让俯仰更容易触发
            if (len <= deadZone) {
                mx = 0.0f;
                my = 0.0f;
            } else {
                // 将 [deadZone, 1] 映射到 [0, 1]
                float clampedLen = (len < 1.0f) ? len : 1.0f;
                float newLen = (clampedLen - deadZone) / (1.0f - deadZone);
                float invLen = 1.0f / len;
                mx = mx * invLen * newLen;
                my = my * invLen * newLen;
            }

            // 为鼠标单独提供俯仰/偏航增益系数
            const float mouseYawGain = 1.5f;   // 再次削弱偏航
            const float mousePitchGain = 40.0f; // 大幅增强俯仰，考虑到玩家纵向移动更少
            mx *= mouseYawGain;
            my *= mousePitchGain;

            // 最终映射到输入，保持 [-1, 1]
            yaw += std::clamp(mx, -1.0f, 1.0f);
            pitch += std::clamp(my, -1.0f, 1.0f);
        }
        
        // 更新组件输入值（可以添加平滑处理）
        spacecraft.inputForward = forward;
        spacecraft.inputStrafe = strafe;
        spacecraft.inputVertical = vertical;
        spacecraft.inputRoll = roll;
        spacecraft.inputPitch = pitch;
        spacecraft.inputYaw = yaw;
    }
}

void SpacecraftDrivingSystem::GetSpacecraftAxes(
    const DirectX::XMFLOAT4& rotation,
    DirectX::XMFLOAT3& forward,
    DirectX::XMFLOAT3& right,
    DirectX::XMFLOAT3& up)
{
    // 从四元数构建旋转矩阵
    XMVECTOR quat = XMLoadFloat4(&rotation);
    XMMATRIX rotMatrix = XMMatrixRotationQuaternion(quat);
    
    // 飞船模型使用标准FBX坐标系：
    // - 模型前方 = +Z 方向（船头朝 +Z）
    // - 模型上方 = +Y 方向（船顶朝 +Y）
    // - 模型右方 = +X 方向
    XMVECTOR fwdVec = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotMatrix);
    XMVECTOR rightVec = XMVector3TransformNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), rotMatrix);
    XMVECTOR upVec = XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), rotMatrix);
    
    XMStoreFloat3(&forward, fwdVec);
    XMStoreFloat3(&right, rightVec);
    XMStoreFloat3(&up, upVec);
}

void SpacecraftDrivingSystem::ApplySpacecraftForces(float deltaTime, entt::registry& registry) {
    auto view = registry.view<SpacecraftComponent, RigidBodyComponent, InSectorComponent>();
    
    for (auto entity : view) {
        auto& spacecraft = view.get<SpacecraftComponent>(entity);
        auto& rigidBody = view.get<RigidBodyComponent>(entity);
        auto& inSector = view.get<InSectorComponent>(entity);
        
        // 只处理 PILOTED 状态
        if (spacecraft.currentState != SpacecraftComponent::State::PILOTED) continue;
        
        // 需要有效的 PhysX actor
        if (!rigidBody.physxActor) continue;
        
        auto* dynamicActor = rigidBody.physxActor->is<physx::PxRigidDynamic>();
        if (!dynamicActor) continue;
        
        // 确保飞船是动态模式（非 kinematic）
        if (dynamicActor->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC) {
            // 切换到动态模式
            dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, false);
            dynamicActor->wakeUp();
        }
        
        // 获取飞船局部坐标轴
        XMFLOAT3 forward, right, up;
        GetSpacecraftAxes(inSector.localRotation, forward, right, up);
        
        // === 计算推力 ===
        XMVECTOR totalForce = XMVectorZero();
        
        // 前后推力 (W/S)
        if (std::abs(spacecraft.inputForward) > 0.001f) {
            XMVECTOR fwdVec = XMLoadFloat3(&forward);
            totalForce = XMVectorAdd(totalForce, 
                XMVectorScale(fwdVec, spacecraft.inputForward * spacecraft.mainThrust));
        }
        
        // 左右推力 (A/D)
        if (std::abs(spacecraft.inputStrafe) > 0.001f) {
            XMVECTOR rightVec = XMLoadFloat3(&right);
            totalForce = XMVectorAdd(totalForce,
                XMVectorScale(rightVec, spacecraft.inputStrafe * spacecraft.strafeThrust));
        }
        
        // 上下推力 (Shift/Ctrl)
        if (std::abs(spacecraft.inputVertical) > 0.001f) {
            XMVECTOR upVec = XMLoadFloat3(&up);
            totalForce = XMVectorAdd(totalForce,
                XMVectorScale(upVec, spacecraft.inputVertical * spacecraft.verticalThrust));
        }
        
        // 应用力
        XMFLOAT3 force;
        XMStoreFloat3(&force, totalForce);
        if (std::abs(force.x) > 0.001f || std::abs(force.y) > 0.001f || std::abs(force.z) > 0.001f) {
            dynamicActor->addForce(physx::PxVec3(force.x, force.y, force.z), physx::PxForceMode::eFORCE);
        }
        
        // === 计算力矩 ===
        XMVECTOR totalTorque = XMVectorZero();
        
        // 滚转力矩 (Q/E) - 绕 forward 轴旋转
        if (std::abs(spacecraft.inputRoll) > 0.001f) {
            XMVECTOR fwdVec = XMLoadFloat3(&forward);
            totalTorque = XMVectorAdd(totalTorque,
                XMVectorScale(fwdVec, spacecraft.inputRoll * spacecraft.rollTorque));
        }
        
        // 俯仰力矩 (↑/↓) - 绕 right 轴旋转
        if (std::abs(spacecraft.inputPitch) > 0.001f) {
            XMVECTOR rightVec = XMLoadFloat3(&right);
            totalTorque = XMVectorAdd(totalTorque,
                XMVectorScale(rightVec, spacecraft.inputPitch * spacecraft.pitchTorque));
        }
        
        // 偏航力矩 (←/→) - 绕 up 轴旋转
        if (std::abs(spacecraft.inputYaw) > 0.001f) {
            XMVECTOR upVec = XMLoadFloat3(&up);
            totalTorque = XMVectorAdd(totalTorque,
                XMVectorScale(upVec, spacecraft.inputYaw * spacecraft.yawTorque));
        }
        
        // 应用力矩
        XMFLOAT3 torque;
        XMStoreFloat3(&torque, totalTorque);
        if (std::abs(torque.x) > 0.001f || std::abs(torque.y) > 0.001f || std::abs(torque.z) > 0.001f) {
            dynamicActor->addTorque(physx::PxVec3(torque.x, torque.y, torque.z), physx::PxForceMode::eFORCE);
        }
        
        // === 调试输出 ===
        static int frameCounter = 0;
        frameCounter++;
        if (frameCounter % 120 == 0) {  // 每2秒输出一次
            physx::PxVec3 vel = dynamicActor->getLinearVelocity();
            physx::PxVec3 angVel = dynamicActor->getAngularVelocity();
            
            char debugMsg[256];
            snprintf(debugMsg, sizeof(debugMsg), 
                "Input: F=%.1f S=%.1f V=%.1f | R=%.1f P=%.1f Y=%.1f | Vel: %.1f, %.1f, %.1f",
                spacecraft.inputForward, spacecraft.inputStrafe, spacecraft.inputVertical,
                spacecraft.inputRoll, spacecraft.inputPitch, spacecraft.inputYaw,
                vel.x, vel.y, vel.z);
            DebugManager::GetInstance().Log("Spacecraft", debugMsg);
        }
    }
}

void SpacecraftDrivingSystem::UpdateSpacecraftState(entt::registry& registry) {
    auto view = registry.view<SpacecraftComponent, RigidBodyComponent>();
    
    for (auto entity : view) {
        auto& spacecraft = view.get<SpacecraftComponent>(entity);
        auto& rigidBody = view.get<RigidBodyComponent>(entity);
        
        if (!rigidBody.physxActor) continue;
        
        auto* dynamicActor = rigidBody.physxActor->is<physx::PxRigidDynamic>();
        if (!dynamicActor) continue;
        
        // 读取当前速度信息
        physx::PxVec3 vel = dynamicActor->getLinearVelocity();
        physx::PxVec3 angVel = dynamicActor->getAngularVelocity();
        
        spacecraft.currentVelocity = { vel.x, vel.y, vel.z };
        spacecraft.currentSpeed = vel.magnitude();
        spacecraft.currentAngularVelocity = { angVel.x, angVel.y, angVel.z };
    }
}

void SpacecraftDrivingSystem::UpdateSpacecraftCamera(float deltaTime, entt::registry& registry) {
    // 查找正在被驾驶的飞船
    auto spacecraftView = registry.view<SpacecraftComponent, TransformComponent, InSectorComponent>();
    
    entt::entity pilotedSpacecraft = entt::null;
    for (auto entity : spacecraftView) {
        auto& spacecraft = spacecraftView.get<SpacecraftComponent>(entity);
        if (spacecraft.currentState == SpacecraftComponent::State::PILOTED) {
            pilotedSpacecraft = entity;
            break;
        }
    }
    
    // 如果没有正在驾驶的飞船，重置相机初始化状态
    if (pilotedSpacecraft == entt::null) {
        m_CameraInitialized = false;
        return;
    }
    
    auto& spacecraft = registry.get<SpacecraftComponent>(pilotedSpacecraft);
    auto& scTransform = registry.get<TransformComponent>(pilotedSpacecraft);
    auto& inSector = registry.get<InSectorComponent>(pilotedSpacecraft);
    
    // 获取扇区世界位置和旋转
    XMFLOAT3 sectorWorldPos = { 0.0f, 0.0f, 0.0f };
    XMFLOAT4 sectorWorldRot = { 0.0f, 0.0f, 0.0f, 1.0f };
    if (inSector.sector != entt::null) {
        auto* sector = registry.try_get<SectorComponent>(inSector.sector);
        if (sector) {
            sectorWorldPos = sector->worldPosition;
            sectorWorldRot = sector->worldRotation;
        }
    }
    
    // 获取飞船局部坐标轴
    XMFLOAT3 forward, right, up;
    GetSpacecraftAxes(inSector.localRotation, forward, right, up);
    
    // 将局部方向转换到世界坐标系（考虑扇区旋转）
    XMVECTOR sectorRotQuat = XMLoadFloat4(&sectorWorldRot);
    XMVECTOR worldForward = XMVector3Rotate(XMLoadFloat3(&forward), sectorRotQuat);
    XMVECTOR worldUp = XMVector3Rotate(XMLoadFloat3(&up), sectorRotQuat);
    
    // 计算飞船世界位置
    XMVECTOR localPos = XMLoadFloat3(&inSector.localPosition);
    XMVECTOR rotatedLocalPos = XMVector3Rotate(localPos, sectorRotQuat);
    XMVECTOR spacecraftWorldPos = XMVectorAdd(rotatedLocalPos, XMLoadFloat3(&sectorWorldPos));
    
    // 计算目标相机位置：飞船后上方
    // 相机位置 = 飞船位置 - forward * distance + up * height
    XMVECTOR targetCameraPos = spacecraftWorldPos;
    targetCameraPos = XMVectorSubtract(targetCameraPos, XMVectorScale(worldForward, spacecraft.cameraDistance));
    targetCameraPos = XMVectorAdd(targetCameraPos, XMVectorScale(worldUp, spacecraft.cameraHeight));
    
    // 相机平滑跟随
    XMVECTOR currentPos = XMLoadFloat3(&m_CurrentCameraPos);
    if (!m_CameraInitialized) {
        // 首次初始化，直接设置到目标位置
        currentPos = targetCameraPos;
        m_CameraInitialized = true;
    } else {
        // 平滑插值到目标位置
        float t = 1.0f - std::exp(-spacecraft.cameraSmoothSpeed * deltaTime);
        currentPos = XMVectorLerp(currentPos, targetCameraPos, t);
    }
    XMStoreFloat3(&m_CurrentCameraPos, currentPos);
    
    // 计算相机注视点：飞船前方一点
    XMVECTOR lookTarget = XMVectorAdd(spacecraftWorldPos, XMVectorScale(worldForward, spacecraft.cameraLookAheadDistance));
    
    // 更新相机组件
    auto cameraView = registry.view<CameraComponent, TransformComponent>();
    for (auto camEntity : cameraView) {
        // 跳过自由相机模式
        auto* freeCamera = registry.try_get<FreeCameraComponent>(camEntity);
        if (freeCamera && freeCamera->isActive) continue;
        
        auto& camera = cameraView.get<CameraComponent>(camEntity);
        auto& camTransform = cameraView.get<TransformComponent>(camEntity);
        
        // 设置相机位置
        XMStoreFloat3(&camTransform.position, currentPos);
        XMStoreFloat3(&camera.position, currentPos);
        
        // 设置相机目标
        XMStoreFloat3(&camera.target, lookTarget);
        
        // 设置相机上方向（使用飞船的上方向）
        XMStoreFloat3(&camera.up, worldUp);
    }
}

} // namespace outer_wilds
