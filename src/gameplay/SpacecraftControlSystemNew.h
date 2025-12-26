#pragma once
#include <entt/entt.hpp>
#include <DirectXMath.h>
#include <PxPhysicsAPI.h>
#include "../core/ECS.h"
#include "../core/TimeManager.h"
#include "components/SpacecraftComponent.h"
#include "../scene/components/TransformComponent.h"
#include "../physics/components/RigidBodyComponent.h"
#include "../physics/components/SectorComponent.h"
#include "../input/InputManager.h"

namespace outer_wilds::systems {

using namespace components;
using namespace DirectX;

/**
 * 飞船控制系统 - 星际拓荒风格
 */
class SpacecraftControlSystemNew {
public:
    explicit SpacecraftControlSystemNew(entt::registry& registry)
        : m_Registry(registry)
    {}
    
    void Update(float deltaTime) {
        auto view = m_Registry.view<SpacecraftComponent, outer_wilds::TransformComponent, RigidBodyComponent>();
        
        for (auto entity : view) {
            auto& spacecraft = view.get<SpacecraftComponent>(entity);
            auto& transform = view.get<outer_wilds::TransformComponent>(entity);
            auto& rigidBody = view.get<RigidBodyComponent>(entity);
            
            if (spacecraft.currentState == SpacecraftComponent::State::IDLE) {
                continue;
            }
            
            CollectInput(spacecraft);
            spacecraft.isBoosting = spacecraft.input.boost;
            
            switch (spacecraft.currentState) {
                case SpacecraftComponent::State::PILOTED:
                    HandlePilotedState(entity, spacecraft, transform, rigidBody, deltaTime);
                    break;
                case SpacecraftComponent::State::LANDING:
                    HandleLandingState(entity, spacecraft, transform, rigidBody, deltaTime);
                    break;
                default:
                    break;
            }
            
            UpdateVelocityInfo(spacecraft, rigidBody);
            CheckStateTransitions(entity, spacecraft, rigidBody);
        }
    }

private:
    entt::registry& m_Registry;
    
    void CollectInput(SpacecraftComponent& spacecraft) {
        auto& input = spacecraft.input;
        auto& inputManager = InputManager::GetInstance();
        
        input = SpacecraftComponent::InputState{};
        
        if (inputManager.IsKeyHeld('W')) input.forward = 1.0f;
        if (inputManager.IsKeyHeld('S')) input.forward = -1.0f;
        if (inputManager.IsKeyHeld('A')) input.strafe = -1.0f;
        if (inputManager.IsKeyHeld('D')) input.strafe = 1.0f;
        
        if (inputManager.IsKeyHeld(VK_SPACE)) input.vertical = 1.0f;
        if (inputManager.IsKeyHeld(VK_CONTROL)) input.vertical = -1.0f;
        
        if (inputManager.IsKeyHeld('Q')) input.roll = -1.0f;
        if (inputManager.IsKeyHeld('E')) input.roll = 1.0f;
        
        input.boost = inputManager.IsKeyHeld(VK_SHIFT);
        input.matchVelocity = inputManager.IsKeyPressed(VK_TAB);
        input.landingMode = inputManager.IsKeyPressed('L');
        
        int mouseX, mouseY;
        inputManager.GetMouseDelta(mouseX, mouseY);
        input.yaw = static_cast<float>(mouseX) * spacecraft.mouseSensitivity;
        input.pitch = -static_cast<float>(mouseY) * spacecraft.mouseSensitivity;
    }
    
    void HandlePilotedState(entt::entity entity, SpacecraftComponent& spacecraft,
                           outer_wilds::TransformComponent& transform, RigidBodyComponent& rigidBody,
                           float deltaTime) {
        if (!rigidBody.physxActor) return;
        
        auto* dynamicActor = rigidBody.physxActor->is<physx::PxRigidDynamic>();
        if (!dynamicActor) return;
        
        ApplyRotation(spacecraft, transform, dynamicActor, deltaTime);
        ApplyThrust(spacecraft, transform, dynamicActor, deltaTime);
        
        if (spacecraft.input.matchVelocity) {
            spacecraft.matchVelocityEnabled = !spacecraft.matchVelocityEnabled;
        }
        
        if (spacecraft.matchVelocityEnabled) {
            ApplyMatchVelocity(spacecraft, transform, dynamicActor, deltaTime);
        }
        
        UpdateSectorAffiliation(entity, spacecraft);
    }
    
    void HandleLandingState(entt::entity entity, SpacecraftComponent& spacecraft,
                           outer_wilds::TransformComponent& transform, RigidBodyComponent& rigidBody,
                           float deltaTime) {
        if (!rigidBody.physxActor) return;
        
        auto* dynamicActor = rigidBody.physxActor->is<physx::PxRigidDynamic>();
        if (!dynamicActor) return;
        
        AlignToSurface(spacecraft, transform, dynamicActor, deltaTime);
        ApplyLandingThrust(spacecraft, transform, dynamicActor, deltaTime);
        
        if (spacecraft.distanceToSurface < 2.0f && spacecraft.currentSpeed < 1.0f) {
            spacecraft.isGrounded = true;
            spacecraft.groundedTime += deltaTime;
            if (spacecraft.groundedTime > 0.5f) {
                spacecraft.currentState = SpacecraftComponent::State::PILOTED;
            }
        } else {
            spacecraft.isGrounded = false;
            spacecraft.groundedTime = 0.0f;
        }
    }
    
    void ApplyThrust(SpacecraftComponent& spacecraft, outer_wilds::TransformComponent& transform,
                    physx::PxRigidDynamic* actor, float deltaTime) {
        XMMATRIX rotMatrix = XMMatrixRotationQuaternion(XMLoadFloat4(&transform.rotation));
        
        XMVECTOR forward = XMVector3Normalize(rotMatrix.r[2]);
        XMVECTOR right = XMVector3Normalize(rotMatrix.r[0]);
        XMVECTOR up = XMVector3Normalize(rotMatrix.r[1]);
        
        XMVECTOR totalForce = XMVectorZero();
        
        if (spacecraft.input.forward != 0.0f) {
            float thrust = spacecraft.GetEffectiveThrust(spacecraft.mainThrustPower);
            totalForce = XMVectorAdd(totalForce, XMVectorScale(forward, spacecraft.input.forward * thrust));
        }
        
        if (spacecraft.input.strafe != 0.0f) {
            float thrust = spacecraft.GetEffectiveThrust(spacecraft.maneuverThrustPower);
            totalForce = XMVectorAdd(totalForce, XMVectorScale(right, spacecraft.input.strafe * thrust));
        }
        
        if (spacecraft.input.vertical != 0.0f) {
            float thrust = spacecraft.GetEffectiveThrust(spacecraft.verticalThrustPower);
            totalForce = XMVectorAdd(totalForce, XMVectorScale(up, spacecraft.input.vertical * thrust));
        }
        
        float mass = actor->getMass();
        XMFLOAT3 forceVec;
        XMStoreFloat3(&forceVec, XMVectorScale(totalForce, mass));
        
        actor->addForce(physx::PxVec3(forceVec.x, forceVec.y, forceVec.z), physx::PxForceMode::eFORCE);
    }
    
    void ApplyRotation(SpacecraftComponent& spacecraft, outer_wilds::TransformComponent& transform,
                      physx::PxRigidDynamic* actor, float deltaTime) {
        XMVECTOR currentRot = XMLoadFloat4(&transform.rotation);
        
        float pitchDelta = spacecraft.input.pitch * spacecraft.pitchSpeed * deltaTime;
        float yawDelta = spacecraft.input.yaw * spacecraft.yawSpeed * deltaTime;
        float rollDelta = spacecraft.input.roll * spacecraft.rollSpeed * deltaTime;
        
        XMVECTOR pitchQuat = XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), pitchDelta);
        XMVECTOR yawQuat = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), yawDelta);
        XMVECTOR rollQuat = XMQuaternionRotationAxis(XMVectorSet(0, 0, 1, 0), rollDelta);
        
        XMVECTOR combinedDelta = XMQuaternionMultiply(yawQuat, pitchQuat);
        combinedDelta = XMQuaternionMultiply(combinedDelta, rollQuat);
        
        XMVECTOR newRot = XMQuaternionMultiply(currentRot, combinedDelta);
        newRot = XMQuaternionNormalize(newRot);
        
        XMStoreFloat4(&transform.rotation, newRot);
        
        auto pose = actor->getGlobalPose();
        pose.q = physx::PxQuat(transform.rotation.x, transform.rotation.y, 
                               transform.rotation.z, transform.rotation.w);
        actor->setGlobalPose(pose);
    }
    
    void ApplyMatchVelocity(SpacecraftComponent& spacecraft, outer_wilds::TransformComponent& transform,
                           physx::PxRigidDynamic* actor, float deltaTime) {
        XMVECTOR currentVel = XMLoadFloat3(&spacecraft.velocity);
        XMVECTOR targetVel = XMLoadFloat3(&spacecraft.targetVelocity);
        XMVECTOR deltaVel = XMVectorSubtract(targetVel, currentVel);
        
        float deltaSpeed = XMVectorGetX(XMVector3Length(deltaVel));
        
        if (deltaSpeed < 0.5f) {
            spacecraft.matchVelocityEnabled = false;
            return;
        }
        
        float maxAccel = spacecraft.mainThrustPower * 0.5f;
        XMVECTOR correctionDir = XMVector3Normalize(deltaVel);
        float correctionAccel = (std::min)(deltaSpeed / deltaTime, maxAccel);
        
        XMVECTOR correctionForce = XMVectorScale(correctionDir, correctionAccel * actor->getMass());
        XMFLOAT3 forceVec;
        XMStoreFloat3(&forceVec, correctionForce);
        
        actor->addForce(physx::PxVec3(forceVec.x, forceVec.y, forceVec.z), physx::PxForceMode::eFORCE);
    }
    
    void AlignToSurface(SpacecraftComponent& spacecraft, outer_wilds::TransformComponent& transform,
                       physx::PxRigidDynamic* actor, float deltaTime) {
        XMVECTOR surfaceNormal = XMLoadFloat3(&spacecraft.surfaceNormal);
        XMVECTOR targetUp = surfaceNormal;
        
        XMMATRIX rotMatrix = XMMatrixRotationQuaternion(XMLoadFloat4(&transform.rotation));
        XMVECTOR currentUp = XMVector3Normalize(rotMatrix.r[1]);
        
        XMVECTOR rotAxis = XMVector3Cross(currentUp, targetUp);
        float dotProduct = XMVectorGetX(XMVector3Dot(currentUp, targetUp));
        dotProduct = (std::max)(-1.0f, (std::min)(1.0f, dotProduct));
        float angle = acosf(dotProduct);
        
        float maxRotation = spacecraft.landingAlignSpeed * deltaTime;
        angle = (std::min)(angle, maxRotation);
        
        if (XMVectorGetX(XMVector3LengthSq(rotAxis)) > 0.0001f) {
            rotAxis = XMVector3Normalize(rotAxis);
            XMVECTOR alignQuat = XMQuaternionRotationAxis(rotAxis, angle);
            
            XMVECTOR currentRot = XMLoadFloat4(&transform.rotation);
            XMVECTOR newRot = XMQuaternionMultiply(alignQuat, currentRot);
            newRot = XMQuaternionNormalize(newRot);
            
            XMStoreFloat4(&transform.rotation, newRot);
            
            auto pose = actor->getGlobalPose();
            pose.q = physx::PxQuat(transform.rotation.x, transform.rotation.y,
                                   transform.rotation.z, transform.rotation.w);
            actor->setGlobalPose(pose);
        }
    }
    
    void ApplyLandingThrust(SpacecraftComponent& spacecraft, outer_wilds::TransformComponent& transform,
                           physx::PxRigidDynamic* actor, float deltaTime) {
        physx::PxVec3 pxVel = actor->getLinearVelocity();
        XMVECTOR velocity = XMVectorSet(pxVel.x, pxVel.y, pxVel.z, 0);
        XMVECTOR surfaceNormal = XMLoadFloat3(&spacecraft.surfaceNormal);
        
        float verticalSpeed = XMVectorGetX(XMVector3Dot(velocity, surfaceNormal));
        float targetDescentSpeed = -spacecraft.landingDescentSpeed;
        
        if (spacecraft.input.vertical > 0) {
            targetDescentSpeed = spacecraft.landingDescentSpeed;
        } else if (spacecraft.input.vertical < 0) {
            targetDescentSpeed = -spacecraft.landingDescentSpeed * 1.5f;
        }
        
        float desiredAccel = (targetDescentSpeed - verticalSpeed) * 3.0f;
        desiredAccel = (std::max)(-spacecraft.verticalThrustPower, (std::min)(spacecraft.verticalThrustPower, desiredAccel));
        
        XMVECTOR force = XMVectorScale(surfaceNormal, desiredAccel * actor->getMass());
        XMFLOAT3 forceVec;
        XMStoreFloat3(&forceVec, force);
        
        actor->addForce(physx::PxVec3(forceVec.x, forceVec.y, forceVec.z), physx::PxForceMode::eFORCE);
    }
    
    void CheckStateTransitions(entt::entity entity, SpacecraftComponent& spacecraft, RigidBodyComponent& rigidBody) {
        if (spacecraft.input.landingMode) {
            if (spacecraft.currentState == SpacecraftComponent::State::PILOTED && spacecraft.IsNearSurface()) {
                spacecraft.currentState = SpacecraftComponent::State::LANDING;
            } else if (spacecraft.currentState == SpacecraftComponent::State::LANDING) {
                spacecraft.currentState = SpacecraftComponent::State::PILOTED;
            }
        }
    }
    
    void UpdateVelocityInfo(SpacecraftComponent& spacecraft, RigidBodyComponent& rigidBody) {
        if (rigidBody.physxActor) {
            auto* dynamicActor = rigidBody.physxActor->is<physx::PxRigidDynamic>();
            if (dynamicActor) {
                physx::PxVec3 vel = dynamicActor->getLinearVelocity();
                spacecraft.velocity = { vel.x, vel.y, vel.z };
                spacecraft.currentSpeed = vel.magnitude();
            }
        }
    }
    
    void UpdateSectorAffiliation(entt::entity entity, SpacecraftComponent& spacecraft) {
        auto* affiliation = m_Registry.try_get<SectorAffiliationComponent>(entity);
        if (affiliation) {
            // 飞船在飞行中始终允许自动切换扇区
            // 这样它可以正确跟随最近的天体
            if (spacecraft.currentState == SpacecraftComponent::State::PILOTED ||
                spacecraft.currentState == SpacecraftComponent::State::LANDING) {
                affiliation->autoSectorSwitch = true;  // 允许自动切换
                affiliation->sectorLocked = false;     // 不锁定
            }
        }
    }
};

} // namespace outer_wilds::systems
