#include "PlayerSystem.h"
#include "../scene/components/TransformComponent.h"
#include "components/PlayerComponent.h"
#include "components/PlayerInputComponent.h"
#include "components/CharacterControllerComponent.h"
#include "../graphics/components/CameraComponent.h"
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

        // Get camera direction vectors
        float yaw = camera.yaw;
        DirectX::XMVECTOR forward = DirectX::XMVectorSet(sinf(yaw), 0.0f, cosf(yaw), 0.0f);
        DirectX::XMVECTOR right = DirectX::XMVectorSet(cosf(yaw), 0.0f, -sinf(yaw), 0.0f);

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
        
        // Handle jumping and gravity
        if (character.wantsToJump && character.isGrounded) {
            character.verticalVelocity = character.jumpSpeed;
            character.wantsToJump = false;
        }
        
        if (!character.isGrounded) {
            character.verticalVelocity += character.gravity * deltaTime;
        } else {
            if (character.verticalVelocity < 0.0f) {
                character.verticalVelocity = 0.0f;
            }
        }
        
        // Combine horizontal and vertical movement
        DirectX::XMFLOAT3 hVel;
        DirectX::XMStoreFloat3(&hVel, horizontalVelocity);
        
        physx::PxVec3 displacement(
            hVel.x * deltaTime,
            character.verticalVelocity * deltaTime,
            hVel.z * deltaTime
        );
        
        // Move the character controller
        physx::PxControllerFilters filters;
        character.controller->move(displacement, 0.0001f, deltaTime, filters);
        
        // Update grounded state
        physx::PxControllerState state;
        character.controller->getState(state);
        character.isGrounded = (state.collisionFlags & physx::PxControllerCollisionFlag::eCOLLISION_DOWN) != 0;
        
        // Update transform to match controller position
        physx::PxExtendedVec3 pos = character.controller->getFootPosition();
        transform.position = {(float)pos.x, (float)pos.y + 0.9f, (float)pos.z}; // +0.9f for eye height
        camera.position = transform.position;

        // Update camera target
        DirectX::XMVECTOR direction = DirectX::XMVectorSet(
            cosf(camera.pitch) * sinf(camera.yaw),
            sinf(camera.pitch),
            cosf(camera.pitch) * cosf(camera.yaw),
            0.0f
        );
        DirectX::XMVECTOR targetPos = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&camera.position), direction);
        DirectX::XMStoreFloat3(&camera.target, targetPos);
        
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

        // Only update camera if mouse look is enabled
        if (input.mouseLookEnabled) {
            // Update camera rotation
            camera.yaw += input.lookInput.x * camera.lookSensitivity;
            camera.pitch -= input.lookInput.y * camera.lookSensitivity; // Inverted Y

            // Constrain pitch to prevent flipping
            const float maxPitch = DirectX::XM_PIDIV2 - 0.1f;  // 89 degrees
            const float minPitch = -DirectX::XM_PIDIV2 + 0.1f; // -89 degrees
            if (camera.pitch > maxPitch) camera.pitch = maxPitch;
            if (camera.pitch < minPitch) camera.pitch = minPitch;
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
