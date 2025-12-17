#include "PlayerSystem.h"
#include "../scene/components/TransformComponent.h"
#include "components/PlayerComponent.h"
#include "components/PlayerInputComponent.h"
#include "components/CharacterControllerComponent.h"
#include "../graphics/components/CameraComponent.h"
#include "../graphics/components/FreeCameraComponent.h"
#include "../physics/components/GravityAffectedComponent.h"
#include "../input/InputManager.h"
#include "../physics/PhysXManager.h"
#include "../core/DebugManager.h"
#include <DirectXMath.h>
#include <iostream>
#include <algorithm>

namespace outer_wilds {

using namespace components;

void PlayerSystem::Initialize() {
    // Initialize player system
}

void PlayerSystem::Initialize(std::shared_ptr<Scene> scene) {
    m_Scene = scene;
    Initialize();
}

void PlayerSystem::Update(float deltaTime, entt::registry& registry) {
    ProcessPlayerInput(deltaTime, registry);
    UpdatePlayerMovement(deltaTime, registry);
    UpdatePlayerCamera(deltaTime, registry);
}

void PlayerSystem::Shutdown() {
    // Cleanup player system
}

void PlayerSystem::ProcessPlayerInput(float deltaTime, entt::registry& registry) {
    // Update input manager
    InputManager::GetInstance().Update();

    // Process input for character controller entities
    auto view = registry.view<components::CharacterControllerComponent, PlayerInputComponent>();
    for (auto entity : view) {
        auto& character = view.get<components::CharacterControllerComponent>(entity);
        auto& playerInput = view.get<PlayerInputComponent>(entity);
        
        // Get fresh input state
        playerInput = InputManager::GetInstance().GetPlayerInput();
        
        // Map input to character controller
        character.forwardInput = playerInput.moveInput.y;   // W/S
        character.rightInput = playerInput.moveInput.x;     // A/D
        character.wantsToJump = playerInput.jumpPressed;
        
        // Handle mouse lock toggle (ESC + Backspace)
        static bool escWasPressed = false;
        static bool backspaceWasPressed = false;
        
        bool escPressed = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        bool backspacePressed = (GetAsyncKeyState(VK_BACK) & 0x8000) != 0;
        
        // Toggle when both keys are pressed together
        if (escPressed && backspacePressed && (!escWasPressed || !backspaceWasPressed)) {
            playerInput.mouseLookEnabled = !playerInput.mouseLookEnabled;
            InputManager::GetInstance().SetMouseLookEnabled(playerInput.mouseLookEnabled);
        }
        
        escWasPressed = escPressed;
        backspaceWasPressed = backspacePressed;
    }
}

void PlayerSystem::UpdatePlayerMovement(float deltaTime, entt::registry& registry) {
    // Update character controller movement
    auto view = registry.view<components::CharacterControllerComponent, TransformComponent, CameraComponent>();
    for (auto entity : view) {
        auto& character = view.get<components::CharacterControllerComponent>(entity);
        auto& transform = view.get<TransformComponent>(entity);
        auto& camera = view.get<CameraComponent>(entity);
        
        if (!character.controller) continue;

        // 跳过自由相机模式
        if (registry.all_of<components::FreeCameraComponent>(entity)) {
            auto& freeCamera = registry.get<components::FreeCameraComponent>(entity);
            if (freeCamera.isActive) continue;
        }

        // === 步骤1：更新局部参考系（基于重力方向） ===
        DirectX::XMVECTOR localUp, localForward, localRight;
        auto* gravity = registry.try_get<components::GravityAffectedComponent>(entity);
        
        if (gravity && gravity->affectedByGravity) {
            // 球面重力模式：局部Up = 重力反方向
            DirectX::XMVECTOR gravityDir = DirectX::XMLoadFloat3(&gravity->currentGravityDir);
            if (DirectX::XMVectorGetX(DirectX::XMVector3Length(gravityDir)) < 0.001f) {
                gravityDir = DirectX::XMVectorSet(0, -1, 0, 0);
            }
            
            // 关键修改：每帧更新CharacterController的up方向以匹配重力
            DirectX::XMFLOAT3 upDirF;
            DirectX::XMStoreFloat3(&upDirF, DirectX::XMVectorNegate(gravityDir));
            physx::PxVec3 pxUp(upDirF.x, upDirF.y, upDirF.z);
            character.controller->setUpDirection(pxUp);
            gravityDir = DirectX::XMVector3Normalize(gravityDir);
            localUp = DirectX::XMVectorNegate(gravityDir);
            
            // 将上一帧的localForward投影到当前切平面，保持方向连续性
            DirectX::XMVECTOR lastLocalForward = DirectX::XMLoadFloat3(&camera.localForward);
            DirectX::XMVECTOR projection = DirectX::XMVectorSubtract(
                lastLocalForward,
                DirectX::XMVectorScale(localUp, DirectX::XMVectorGetX(DirectX::XMVector3Dot(lastLocalForward, localUp)))
            );
            
            float projLength = DirectX::XMVectorGetX(DirectX::XMVector3Length(projection));
            if (projLength > 0.01f) {
                localForward = DirectX::XMVector3Normalize(projection);
            } else {
                // 投影失败（几乎垂直），使用世界参考重新初始化
                DirectX::XMVECTOR worldRef = DirectX::XMVectorSet(0, 0, 1, 0);
                if (abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(localUp, worldRef))) > 0.99f) {
                    worldRef = DirectX::XMVectorSet(1, 0, 0, 0);
                }
                projection = DirectX::XMVectorSubtract(
                    worldRef,
                    DirectX::XMVectorScale(localUp, DirectX::XMVectorGetX(DirectX::XMVector3Dot(worldRef, localUp)))
                );
                localForward = DirectX::XMVector3Normalize(projection);
            }
            
            localRight = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(localUp, localForward));
            
            // 存储局部坐标系供下一帧使用
            DirectX::XMStoreFloat3(&camera.localUp, localUp);
            DirectX::XMStoreFloat3(&camera.localForward, localForward);
            DirectX::XMStoreFloat3(&camera.localRight, localRight);
        } else {
            // 平面重力模式：使用世界坐标系
            localUp = DirectX::XMVectorSet(0, 1, 0, 0);
            localForward = DirectX::XMVectorSet(0, 0, 1, 0);
            localRight = DirectX::XMVectorSet(1, 0, 0, 0);
        }

        // === 步骤2：计算最终视线方向（局部坐标系 + 相对旋转） ===
        // 2.1 应用相对Yaw（水平旋转）
        DirectX::XMVECTOR yawQuat = DirectX::XMQuaternionRotationAxis(localUp, camera.relativeYaw);
        DirectX::XMVECTOR yawedForward = DirectX::XMVector3Rotate(localForward, yawQuat);
        DirectX::XMVECTOR yawedRight = DirectX::XMVector3Rotate(localRight, yawQuat);
        
        // 2.2 应用相对Pitch（俯仰旋转）
        DirectX::XMVECTOR pitchQuat = DirectX::XMQuaternionRotationAxis(yawedRight, camera.relativePitch);
        DirectX::XMVECTOR finalDirection = DirectX::XMVector3Normalize(DirectX::XMVector3Rotate(yawedForward, pitchQuat));
        
        // 2.3 计算相机Up向量（避免LookAt奇点）
        DirectX::XMVECTOR cameraUp = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(finalDirection, yawedRight));
        DirectX::XMStoreFloat3(&camera.up, cameraUp);
        
        // === 步骤3：计算移动方向（使用yawedForward，忽略Pitch） ===
        DirectX::XMVECTOR forward = yawedForward;  // 水平前方
        DirectX::XMVECTOR right = yawedRight;      // 水平右方

        // Calculate horizontal movement
        DirectX::XMVECTOR moveDir = DirectX::XMVectorAdd(
            DirectX::XMVectorScale(forward, character.forwardInput),
            DirectX::XMVectorScale(right, character.rightInput)
        );
        
        // Normalize to prevent faster diagonal movement
        float len = DirectX::XMVectorGetX(DirectX::XMVector3Length(moveDir));
        if (len > 0.01f) {
            moveDir = DirectX::XMVectorScale(moveDir, 1.0f / len);
        }
        
        DirectX::XMVECTOR horizontalVelocity = DirectX::XMVectorScale(moveDir, character.moveSpeed);
        
        // Get current foot position for ground detection
        physx::PxExtendedVec3 pxFootPos = character.controller->getFootPosition();
        DirectX::XMVECTOR footPos = DirectX::XMVectorSet((float)pxFootPos.x, (float)pxFootPos.y, (float)pxFootPos.z, 0.0f);
        
        // Handle jumping and gravity
        float gravityStrength = (gravity && gravity->affectedByGravity) ? 
            gravity->currentGravityStrength * gravity->gravityScale : 20.0f;
        
        // 使用PhysX原生接地检测（现在up方向已正确设置）
        physx::PxControllerState state;
        character.controller->getState(state);
        character.isGrounded = (state.collisionFlags & physx::PxControllerCollisionFlag::eCOLLISION_DOWN) != 0;
        
        if (character.wantsToJump && character.isGrounded) {
            character.verticalVelocity = character.jumpSpeed;
            character.wantsToJump = false;
        }
        
        if (!character.isGrounded) {
            character.verticalVelocity -= gravityStrength * deltaTime;
        } else {
            if (character.verticalVelocity < 0.0f) {
                character.verticalVelocity = 0.0f;
            }
        }
        
        // 组合水平和垂直速度（CharacterController会自动处理碰撞）
        DirectX::XMVECTOR verticalVelocity = DirectX::XMVectorScale(localUp, character.verticalVelocity);
        DirectX::XMVECTOR totalVelocity = DirectX::XMVectorAdd(horizontalVelocity, verticalVelocity);
        
        DirectX::XMVECTOR displacementVec = DirectX::XMVectorScale(totalVelocity, deltaTime);
        
        DirectX::XMFLOAT3 disp;
        DirectX::XMStoreFloat3(&disp, displacementVec);
        physx::PxVec3 displacement(disp.x, disp.y, disp.z);
        
        // Move the character controller
        physx::PxControllerFilters filters;
        character.controller->move(displacement, 0.0001f, deltaTime, filters);
        
        // Note: isGrounded is now set by custom raycast ground detection above
        // (Removed PhysX's world-space collision detection as it fails on spherical surfaces)
        
        // Re-query foot position after move (it may have changed)
        physx::PxExtendedVec3 pos = character.controller->getFootPosition();
        footPos = DirectX::XMVectorSet((float)pos.x, (float)pos.y, (float)pos.z, 0.0f);
        
        // 眼睛位置 = 脚位置 + localUp方向偏移
        DirectX::XMVECTOR eyePos = DirectX::XMVectorAdd(footPos, DirectX::XMVectorScale(localUp, 0.9f));
        DirectX::XMStoreFloat3(&transform.position, eyePos);
        DirectX::XMStoreFloat3(&camera.position, eyePos);

        // Update camera target (使用finalDirection)
        DirectX::XMVECTOR targetPos = DirectX::XMVectorAdd(eyePos, finalDirection);
        DirectX::XMStoreFloat3(&camera.target, targetPos);
        
        // === 同步视觉模型实体位置 ===
        // 只在玩家相机激活时同步(避免自由相机模式下误同步)
        if (camera.isActive) {
            auto* playerComp = registry.try_get<PlayerComponent>(entity);
            if (playerComp && playerComp->visualModelEntity != entt::null) {
                auto* modelTransform = registry.try_get<TransformComponent>(playerComp->visualModelEntity);
                if (modelTransform) {
                    // 视觉模型位置 = 脚位置 (不是眼睛位置)
                    DirectX::XMStoreFloat3(&modelTransform->position, footPos);
                    
                    // 可选: 根据移动方向旋转模型
                    // 这里暂时不旋转,保持默认朝向
                }
            }
        }
        
        // Reset inputs
        character.forwardInput = 0.0f;
        character.rightInput = 0.0f;
    }
}

void PlayerSystem::UpdatePlayerCamera(float deltaTime, entt::registry& registry) {
    // Update first-person camera based on mouse input
    auto view = registry.view<CameraComponent, PlayerInputComponent>();
    for (auto entity : view) {
        auto& camera = view.get<CameraComponent>(entity);
        auto& input = view.get<PlayerInputComponent>(entity);

        // 跳过自由相机模式
        if (registry.all_of<components::FreeCameraComponent>(entity)) {
            auto& freeCamera = registry.get<components::FreeCameraComponent>(entity);
            if (freeCamera.isActive) continue;
        }

        // Only update camera if mouse look is enabled
        if (input.mouseLookEnabled) {
            // === 核心改动：修改相对旋转，而非绝对旋转 ===
            camera.relativeYaw += input.lookInput.x * camera.lookSensitivity;
            camera.relativePitch += input.lookInput.y * camera.lookSensitivity; // 不反向

            // 限制相对Pitch避免万向锁
            const float maxPitch = DirectX::XM_PIDIV2 - 0.1f;  // 89度
            const float minPitch = -DirectX::XM_PIDIV2 + 0.1f; // -89度
            if (camera.relativePitch > maxPitch) camera.relativePitch = maxPitch;
            if (camera.relativePitch < minPitch) camera.relativePitch = minPitch;
            
            // 保留旧的yaw/pitch以兼容平面模式（调试用）
            camera.yaw = camera.relativeYaw;
            camera.pitch = camera.relativePitch;
        }
    }
}

entt::entity PlayerSystem::FindPlayerCamera(entt::entity playerEntity, entt::registry& registry) {
    // For now, assume camera is a separate entity
    auto cameraView = registry.view<CameraComponent>();
    if (!cameraView.empty()) {
        return *cameraView.begin();
    }
    return entt::null;
}

entt::entity PlayerSystem::FindCameraPlayer(entt::entity cameraEntity, entt::registry& registry) {
    // For now, assume player is a separate entity
    auto playerView = registry.view<PlayerInputComponent>();
    if (!playerView.empty()) {
        return *playerView.begin();
    }
    return entt::null;
}

} // namespace outer_wilds
