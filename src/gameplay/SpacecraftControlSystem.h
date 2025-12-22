#pragma once
#include "../core/ECS.h"
#include "../input/InputManager.h"
#include "../scene/components/TransformComponent.h"
#include "../physics/components/GravityAffectedComponent.h"
#include "../physics/components/GravitySourceComponent.h"
#include "../physics/components/ReferenceFrameComponent.h"
#include "components/SpacecraftComponent.h"
#include "../physics/components/RigidBodyComponent.h"
#include <DirectXMath.h>
#include <PxPhysicsAPI.h>
#include <cfloat>
#include <algorithm>
#include <cmath>

namespace outer_wilds {

/**
 * 飞船控制系统：
 * - 收集输入（WASD/Shift/Ctrl + 鼠标）
 * - 应用角加速度模型控制朝向
 * - 通过PhysX动力学推进飞船
 * - 低空时自动切换到辅助起降模式
 */
class SpacecraftControlSystem : public System {
public:
    void Update(float deltaTime, entt::registry& registry) override;

private:
    using Spacecraft = components::SpacecraftComponent;
    using Gravity = components::GravityAffectedComponent;
    using Attached = components::AttachedToReferenceFrameComponent;

    void CollectInput(Spacecraft& spacecraft) const;
    void UpdateAngularVelocity(float deltaTime, Spacecraft& spacecraft) const;
    void UpdateFlightMode(Spacecraft& spacecraft,
                          TransformComponent& transform,
                          entt::registry& registry) const;

    void HandlePilotedState(float deltaTime,
                            Spacecraft& spacecraft,
                            RigidBodyComponent& rigidBody,
                            TransformComponent& transform,
                            Gravity& gravity,
                            physx::PxRigidDynamic& actor) const;

    void HandleLandingAssistState(float deltaTime,
                                  Spacecraft& spacecraft,
                                  RigidBodyComponent& rigidBody,
                                  TransformComponent& transform,
                                  Gravity& gravity,
                                  physx::PxRigidDynamic& actor) const;
    
    // 仅应用旋转（用于地面等待时的视角控制）
    void ApplyRotationOnly(float deltaTime,
                           Spacecraft& spacecraft,
                           TransformComponent& transform,
                           Gravity& gravity,
                           physx::PxRigidDynamic& actor) const;

    void SyncTransformFromActor(TransformComponent& transform,
                                const physx::PxRigidDynamic& actor) const;

    physx::PxVec3 GetReferenceFrameVelocity(const Attached* attached,
                                            entt::registry& registry) const;
};

inline void SpacecraftControlSystem::Update(float deltaTime, entt::registry& registry) {
    auto view = registry.view<Spacecraft, RigidBodyComponent, TransformComponent, Gravity>();

    for (auto entity : view) {
        auto& spacecraft = view.get<Spacecraft>(entity);
        auto& rigidBody = view.get<RigidBodyComponent>(entity);
        auto& transform = view.get<TransformComponent>(entity);
        auto& gravity = view.get<Gravity>(entity);
        auto* attached = registry.try_get<Attached>(entity);

        if (spacecraft.currentState == Spacecraft::State::IDLE) {
            continue;
        }

        if (!rigidBody.physxActor) {
            continue;
        }

        auto* dynamicActor = rigidBody.physxActor->is<physx::PxRigidDynamic>();
        if (!dynamicActor) {
            continue;
        }

        CollectInput(spacecraft);
        UpdateAngularVelocity(deltaTime, spacecraft);
        UpdateFlightMode(spacecraft, transform, registry);

        // 调试输出
        static int debugFrame = 0;
        bool shouldDebug = (++debugFrame % 60 == 0);

        bool waitingForLiftoff = attached && attached->staticAttach;
        if (waitingForLiftoff) {
            bool hasMovementInput = (std::abs(spacecraft.inputForward) > 0.01f) ||
                            (std::abs(spacecraft.inputStrafe) > 0.01f) ||
                            (std::abs(spacecraft.inputVertical) > 0.01f);
            
            if (shouldDebug) {
                std::cout << "[SpacecraftControl] Waiting for liftoff, hasMovementInput=" << hasMovementInput
                         << " (fwd=" << spacecraft.inputForward 
                         << ", str=" << spacecraft.inputStrafe
                         << ", vert=" << spacecraft.inputVertical << ")" << std::endl;
            }
            
            if (!hasMovementInput) {
                // 保持 kinematic 状态，但仍然允许旋转
                if (!(dynamicActor->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)) {
                    dynamicActor->setLinearVelocity(physx::PxVec3(0, 0, 0));
                    dynamicActor->setAngularVelocity(physx::PxVec3(0, 0, 0));
                    dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
                    rigidBody.isKinematic = true;
                }
                
                // 即使在地面等待，也允许旋转飞船（用于瞄准）
                ApplyRotationOnly(deltaTime, spacecraft, transform, gravity, *dynamicActor);
                continue;  // 跳过位移物理更新
            }

            // 玩家按下了移动键，准备起飞
            std::cout << "[SpacecraftControl] LIFTOFF! Switching to dynamic mode" << std::endl;
            
            attached->staticAttach = false;
            attached->groundStableTime = 0.0f;

            // 获取参考系速度并切换到动态模式
            physx::PxVec3 inheritedVelocity = GetReferenceFrameVelocity(attached, registry);
            float inheritedSpeed = inheritedVelocity.magnitude();
            dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, false);
            rigidBody.isKinematic = false;
            dynamicActor->setLinearVelocity(inheritedVelocity);
            rigidBody.velocity = { inheritedVelocity.x, inheritedVelocity.y, inheritedVelocity.z };
            dynamicActor->setAngularVelocity(physx::PxVec3(0, 0, 0));
            dynamicActor->wakeUp();
            
            std::cout << "  - Inherited velocity: (" << inheritedVelocity.x << ", " 
                     << inheritedVelocity.y << ", " << inheritedVelocity.z << ")" << std::endl;
            std::cout << "  - Inherited speed: " << inheritedSpeed << " m/s (this is the planet's orbital velocity)" << std::endl;
        } else {
            if (rigidBody.isKinematic ||
                (dynamicActor->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)) {
                dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, false);
                rigidBody.isKinematic = false;
            }
        }

        if (spacecraft.currentState == Spacecraft::State::LANDING_ASSIST && spacecraft.landingAssistAvailable) {
            HandleLandingAssistState(deltaTime, spacecraft, rigidBody, transform, gravity, *dynamicActor);
        } else {
            HandlePilotedState(deltaTime, spacecraft, rigidBody, transform, gravity, *dynamicActor);
        }

        SyncTransformFromActor(transform, *dynamicActor);

        physx::PxVec3 pxVel = dynamicActor->getLinearVelocity();
        rigidBody.velocity = { pxVel.x, pxVel.y, pxVel.z };
    }
}

inline void SpacecraftControlSystem::CollectInput(Spacecraft& spacecraft) const {
    auto& input = InputManager::GetInstance();

    spacecraft.inputForward = 0.0f;
    spacecraft.inputStrafe = 0.0f;
    spacecraft.inputVertical = 0.0f;

    if (input.IsKeyHeld('W')) spacecraft.inputForward += 1.0f;
    if (input.IsKeyHeld('S')) spacecraft.inputForward -= 1.0f;
    if (input.IsKeyHeld('A')) spacecraft.inputStrafe -= 1.0f;
    if (input.IsKeyHeld('D')) spacecraft.inputStrafe += 1.0f;
    if (input.IsKeyHeld(VK_SHIFT)) spacecraft.inputVertical += 1.0f;
    if (input.IsKeyHeld(VK_CONTROL)) spacecraft.inputVertical -= 1.0f;

    // 使用 PlayerInputComponent 的 lookInput（基于窗口中心的鼠标偏移）
    // 而不是 GetMouseDelta（帧间差值，在鼠标锁定模式下不正确）
    auto& playerInput = input.GetPlayerInput();
    spacecraft.inputYaw = playerInput.lookInput.x * spacecraft.mouseSensitivity;
    spacecraft.inputPitch = playerInput.lookInput.y * spacecraft.mouseSensitivity;
}

inline void SpacecraftControlSystem::UpdateAngularVelocity(float deltaTime, Spacecraft& spacecraft) const {
    // 鼠标输入直接作为角速度（inputYaw/inputPitch 已经乘过 mouseSensitivity）
    // 这给予更直接的控制感
    spacecraft.currentYawSpeed = spacecraft.inputYaw * 10.0f;   // 放大输入
    spacecraft.currentPitchSpeed = spacecraft.inputPitch * 10.0f;

    // 限制最大角速度
    spacecraft.currentYawSpeed = std::clamp(spacecraft.currentYawSpeed,
                                            -spacecraft.maxAngularSpeed,
                                            spacecraft.maxAngularSpeed);
    spacecraft.currentPitchSpeed = std::clamp(spacecraft.currentPitchSpeed,
                                              -spacecraft.maxAngularSpeed,
                                              spacecraft.maxAngularSpeed);
}

inline void SpacecraftControlSystem::UpdateFlightMode(Spacecraft& spacecraft,
                                                      TransformComponent& transform,
                                                      entt::registry& registry) const {
    float minDistance = FLT_MAX;
    entt::entity nearestCelestial = entt::null;

    auto celestialView = registry.view<components::GravitySourceComponent, TransformComponent>();
    DirectX::XMVECTOR craftPos = DirectX::XMLoadFloat3(&transform.position);

    for (auto celestial : celestialView) {
        auto& celestialTransform = celestialView.get<TransformComponent>(celestial);
        auto& gravitySource = celestialView.get<components::GravitySourceComponent>(celestial);

        DirectX::XMVECTOR celestialPos = DirectX::XMLoadFloat3(&celestialTransform.position);
        float distance = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(craftPos, celestialPos)));
        float surfaceDistance = distance - gravitySource.radius;

        if (surfaceDistance < minDistance) {
            minDistance = surfaceDistance;
            nearestCelestial = celestial;
        }
    }

    spacecraft.nearestCelestial = nearestCelestial;
    spacecraft.distanceToSurface = minDistance;
    bool assistable = (nearestCelestial != entt::null &&
                       minDistance > 0.0f &&
                       minDistance < spacecraft.landingAssistAltitude);
    spacecraft.landingAssistAvailable = assistable;

    if (assistable && spacecraft.currentState == Spacecraft::State::PILOTED) {
        spacecraft.currentState = Spacecraft::State::LANDING_ASSIST;
    } else if (!assistable && spacecraft.currentState == Spacecraft::State::LANDING_ASSIST) {
        spacecraft.currentState = Spacecraft::State::PILOTED;
    }
}

inline void SpacecraftControlSystem::HandlePilotedState(float deltaTime,
                                                        Spacecraft& spacecraft,
                                                        RigidBodyComponent& rigidBody,
                                                        TransformComponent& transform,
                                                        Gravity& gravity,
                                                        physx::PxRigidDynamic& actor) const {
    // 确保不是 kinematic 模式（HandlePilotedState 只应在动态模式下调用）
    if (actor.getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC) {
        std::cout << "[SpacecraftControl] WARNING: HandlePilotedState called on kinematic actor!" << std::endl;
        return;
    }
    
    // 获取当前旋转四元数
    DirectX::XMVECTOR currentQuat = DirectX::XMLoadFloat4(&transform.rotation);
    
    // 确定本地 "上" 方向
    DirectX::XMVECTOR gravityDir = DirectX::XMLoadFloat3(&gravity.currentGravityDir);
    float gravityMagnitude = DirectX::XMVectorGetX(DirectX::XMVector3Length(gravityDir));
    bool hasGravity = (gravityMagnitude > 0.01f);
    
    DirectX::XMVECTOR localUp;
    if (hasGravity) {
        // 在星球附近：使用重力反方向作为 "上"
        localUp = DirectX::XMVector3Normalize(DirectX::XMVectorNegate(gravityDir));
    } else {
        // 在太空中：使用飞船当前的上方向
        localUp = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 1, 0, 0), currentQuat);
    }
    
    // 获取飞船当前的前方向
    DirectX::XMVECTOR currentForward = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), currentQuat);
    DirectX::XMVECTOR currentRight = DirectX::XMVector3Rotate(DirectX::XMVectorSet(1, 0, 0, 0), currentQuat);
    DirectX::XMVECTOR currentUp = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 1, 0, 0), currentQuat);
    
    // 增量旋转：绕本地上轴旋转 yaw，绕本地右轴旋转 pitch
    // 使用当前帧的角速度，而不是累加的角度
    DirectX::XMVECTOR yawQuat = DirectX::XMQuaternionRotationAxis(currentUp, spacecraft.currentYawSpeed * deltaTime);
    DirectX::XMVECTOR pitchQuat = DirectX::XMQuaternionRotationAxis(currentRight, spacecraft.currentPitchSpeed * deltaTime);
    
    // 应用增量旋转
    DirectX::XMVECTOR newQuat = DirectX::XMQuaternionMultiply(currentQuat, yawQuat);
    newQuat = DirectX::XMQuaternionMultiply(newQuat, pitchQuat);
    newQuat = DirectX::XMQuaternionNormalize(newQuat);
    
    DirectX::XMFLOAT4 newQuatF;
    DirectX::XMStoreFloat4(&newQuatF, newQuat);

    physx::PxTransform pose = actor.getGlobalPose();
    pose.q = physx::PxQuat(newQuatF.x, newQuatF.y, newQuatF.z, newQuatF.w);
    actor.setGlobalPose(pose);
    actor.setAngularVelocity(physx::PxVec3(0.0f, 0.0f, 0.0f));
    transform.rotation = newQuatF;

    // 更新方向向量
    DirectX::XMVECTOR forward = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), newQuat);
    DirectX::XMVECTOR right = DirectX::XMVector3Rotate(DirectX::XMVectorSet(1, 0, 0, 0), newQuat);
    DirectX::XMVECTOR up = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 1, 0, 0), newQuat);

    // 计算推力方向
    DirectX::XMVECTOR thrust = DirectX::XMVectorZero();
    
    // 太空中降低加速效果
    float thrustMultiplier = hasGravity ? 1.0f : 0.5f;
    
    thrust = DirectX::XMVectorAdd(thrust, DirectX::XMVectorScale(forward, spacecraft.inputForward * spacecraft.thrustPower * thrustMultiplier));
    thrust = DirectX::XMVectorAdd(thrust, DirectX::XMVectorScale(right, spacecraft.inputStrafe * spacecraft.thrustPower * thrustMultiplier));
    thrust = DirectX::XMVectorAdd(thrust, DirectX::XMVectorScale(up, spacecraft.inputVertical * spacecraft.verticalThrustPower * thrustMultiplier));

    thrust = DirectX::XMVectorScale(thrust, rigidBody.mass);
    DirectX::XMFLOAT3 thrustF;
    DirectX::XMStoreFloat3(&thrustF, thrust);

    actor.addForce(physx::PxVec3(thrustF.x, thrustF.y, thrustF.z), physx::PxForceMode::eFORCE);

    // 速度阻尼 - 太空中增加阻尼防止失控
    physx::PxVec3 currentVel = actor.getLinearVelocity();
    float dampingFactor = hasGravity ? rigidBody.drag : (rigidBody.drag * 3.0f);
    physx::PxVec3 damping = currentVel * (-dampingFactor * deltaTime);
    actor.addForce(damping, physx::PxForceMode::eVELOCITY_CHANGE);
}

inline void SpacecraftControlSystem::HandleLandingAssistState(float deltaTime,
                                                              Spacecraft& spacecraft,
                                                              RigidBodyComponent& rigidBody,
                                                              TransformComponent& transform,
                                                              Gravity& gravity,
                                                              physx::PxRigidDynamic& actor) const {
    // 确保不是 kinematic 模式
    if (actor.getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC) {
        std::cout << "[SpacecraftControl] WARNING: HandleLandingAssistState called on kinematic actor!" << std::endl;
        return;
    }
    
    // 获取重力方向和上方向
    DirectX::XMVECTOR gravityDir = DirectX::XMLoadFloat3(&gravity.currentGravityDir);
    if (DirectX::XMVectorGetX(DirectX::XMVector3Length(gravityDir)) < 0.001f) {
        gravityDir = DirectX::XMVectorSet(0, -1, 0, 0);
    }
    DirectX::XMVECTOR upDir = DirectX::XMVector3Normalize(DirectX::XMVectorNegate(gravityDir));

    // === 自动对齐飞船到重力垂直方向 ===
    DirectX::XMVECTOR defaultUp = DirectX::XMVectorSet(0, 1, 0, 0);
    DirectX::XMVECTOR alignQuat = DirectX::XMQuaternionIdentity();
    float dot = DirectX::XMVectorGetX(DirectX::XMVector3Dot(defaultUp, upDir));

    if (dot < -0.9999f) {
        alignQuat = DirectX::XMQuaternionRotationAxis(DirectX::XMVectorSet(1, 0, 0, 0), DirectX::XM_PI);
    } else if (dot < 0.9999f) {
        DirectX::XMVECTOR axis = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(defaultUp, upDir));
        float angle = std::acosf(std::clamp(dot, -1.0f, 1.0f));
        alignQuat = DirectX::XMQuaternionRotationAxis(axis, angle);
    }

    // 增量旋转 yaw（绕本地上轴）
    DirectX::XMVECTOR currentQuat = DirectX::XMLoadFloat4(&transform.rotation);
    DirectX::XMVECTOR currentUp = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 1, 0, 0), currentQuat);
    DirectX::XMVECTOR yawQuat = DirectX::XMQuaternionRotationAxis(currentUp, spacecraft.currentYawSpeed * deltaTime);
    
    // 目标姿态 = 对齐到重力垂直
    DirectX::XMVECTOR targetQuat = DirectX::XMQuaternionNormalize(alignQuat);
    
    // 平滑过渡到目标姿态
    DirectX::XMVECTOR newQuat = DirectX::XMQuaternionSlerp(currentQuat, targetQuat,
        std::clamp(spacecraft.landingAlignSpeed * deltaTime, 0.0f, 1.0f));
    // 应用 yaw 旋转
    newQuat = DirectX::XMQuaternionMultiply(newQuat, yawQuat);
    newQuat = DirectX::XMQuaternionNormalize(newQuat);

    DirectX::XMFLOAT4 newQuatF;
    DirectX::XMStoreFloat4(&newQuatF, newQuat);

    physx::PxTransform pose = actor.getGlobalPose();
    pose.q = physx::PxQuat(newQuatF.x, newQuatF.y, newQuatF.z, newQuatF.w);
    actor.setGlobalPose(pose);
    actor.setAngularVelocity(physx::PxVec3(0.0f, 0.0f, 0.0f));
    transform.rotation = newQuatF;

    // === 速度匹配：消除相对于星球表面的水平速度 ===
    physx::PxVec3 vel = actor.getLinearVelocity();
    DirectX::XMFLOAT3 upF;
    DirectX::XMStoreFloat3(&upF, upDir);
    physx::PxVec3 upPx(upF.x, upF.y, upF.z);
    
    // 分解速度为垂直和水平分量
    float verticalSpeed = vel.dot(upPx);
    physx::PxVec3 verticalVel = upPx * verticalSpeed;
    physx::PxVec3 horizontalVel = vel - verticalVel;
    
    // === 推力处理 ===
    DirectX::XMVECTOR forward = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), newQuat);
    DirectX::XMVECTOR right = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(upDir, forward));
    forward = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(right, upDir));

    // 水平移动（W/S/A/D）- 在辅助起降模式下也允许微调
    if (std::abs(spacecraft.inputForward) > 0.01f || std::abs(spacecraft.inputStrafe) > 0.01f) {
        DirectX::XMVECTOR horizontalThrust = DirectX::XMVectorZero();
        horizontalThrust = DirectX::XMVectorAdd(horizontalThrust, DirectX::XMVectorScale(forward, spacecraft.inputForward * spacecraft.thrustPower * 0.3f));
        horizontalThrust = DirectX::XMVectorAdd(horizontalThrust, DirectX::XMVectorScale(right, spacecraft.inputStrafe * spacecraft.thrustPower * 0.3f));
        horizontalThrust = DirectX::XMVectorScale(horizontalThrust, rigidBody.mass);
        DirectX::XMFLOAT3 htF;
        DirectX::XMStoreFloat3(&htF, horizontalThrust);
        actor.addForce(physx::PxVec3(htF.x, htF.y, htF.z), physx::PxForceMode::eFORCE);
    } else {
        // 无水平输入时，自动消除水平速度（匹配星球表面）
        horizontalVel *= std::exp(-5.0f * deltaTime);  // 快速衰减水平速度
    }
    
    // 垂直推力（Shift/Ctrl）- 沿重力垂线上升/下降
    if (std::abs(spacecraft.inputVertical) > 0.01f) {
        DirectX::XMVECTOR verticalThrust = DirectX::XMVectorScale(upDir, spacecraft.inputVertical * spacecraft.verticalThrustPower);
        verticalThrust = DirectX::XMVectorScale(verticalThrust, rigidBody.mass);
        DirectX::XMFLOAT3 vtF;
        DirectX::XMStoreFloat3(&vtF, verticalThrust);
        actor.addForce(physx::PxVec3(vtF.x, vtF.y, vtF.z), physx::PxForceMode::eFORCE);
    }
    
    // 应用修正后的速度（保留垂直速度，衰减水平速度）
    physx::PxVec3 correctedVel = verticalVel + horizontalVel;
    actor.setLinearVelocity(correctedVel);
}

inline void SpacecraftControlSystem::SyncTransformFromActor(TransformComponent& transform,
                                                            const physx::PxRigidDynamic& actor) const {
    const physx::PxTransform pose = actor.getGlobalPose();
    transform.position = { pose.p.x, pose.p.y, pose.p.z };
    transform.rotation = { pose.q.x, pose.q.y, pose.q.z, pose.q.w };
}

inline void SpacecraftControlSystem::ApplyRotationOnly(float deltaTime,
                                                       Spacecraft& spacecraft,
                                                       TransformComponent& transform,
                                                       Gravity& gravity,
                                                       physx::PxRigidDynamic& actor) const {
    // 获取当前旋转四元数
    DirectX::XMVECTOR currentQuat = DirectX::XMLoadFloat4(&transform.rotation);
    
    // 获取飞船当前的本地轴
    DirectX::XMVECTOR currentRight = DirectX::XMVector3Rotate(DirectX::XMVectorSet(1, 0, 0, 0), currentQuat);
    DirectX::XMVECTOR currentUp = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 1, 0, 0), currentQuat);
    
    // 增量旋转：绕本地上轴旋转 yaw，绕本地右轴旋转 pitch
    DirectX::XMVECTOR yawQuat = DirectX::XMQuaternionRotationAxis(currentUp, spacecraft.currentYawSpeed * deltaTime);
    DirectX::XMVECTOR pitchQuat = DirectX::XMQuaternionRotationAxis(currentRight, spacecraft.currentPitchSpeed * deltaTime);
    
    // 应用增量旋转
    DirectX::XMVECTOR newQuat = DirectX::XMQuaternionMultiply(currentQuat, yawQuat);
    newQuat = DirectX::XMQuaternionMultiply(newQuat, pitchQuat);
    newQuat = DirectX::XMQuaternionNormalize(newQuat);
    
    DirectX::XMFLOAT4 newQuatF;
    DirectX::XMStoreFloat4(&newQuatF, newQuat);

    // 更新 Transform
    transform.rotation = newQuatF;
    
    // 同步到 PhysX actor（对于 kinematic 物体使用 setGlobalPose）
    physx::PxTransform pose = actor.getGlobalPose();
    pose.q = physx::PxQuat(newQuatF.x, newQuatF.y, newQuatF.z, newQuatF.w);
    actor.setGlobalPose(pose);
}

inline physx::PxVec3 SpacecraftControlSystem::GetReferenceFrameVelocity(const Attached* attached,
                                                                        entt::registry& registry) const {
    if (!attached || attached->referenceFrame == entt::null) {
        return physx::PxVec3(0.0f, 0.0f, 0.0f);
    }
    auto* ref = registry.try_get<components::ReferenceFrameComponent>(attached->referenceFrame);
    if (!ref) {
        return physx::PxVec3(0.0f, 0.0f, 0.0f);
    }
    return physx::PxVec3(ref->velocity.x, ref->velocity.y, ref->velocity.z);
}

} // namespace outer_wilds
