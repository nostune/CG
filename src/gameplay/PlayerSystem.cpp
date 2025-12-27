#include "PlayerSystem.h"
#include "../scene/components/TransformComponent.h"
#include "components/PlayerComponent.h"
#include "components/PlayerInputComponent.h"
#include "components/CharacterControllerComponent.h"
#include "components/SpacecraftComponent.h"
#include "../graphics/components/CameraComponent.h"
#include "../graphics/components/FreeCameraComponent.h"
#include "../physics/components/GravityAffectedComponent.h"
#include "../physics/components/SectorComponent.h"
#include "../physics/components/RigidBodyComponent.h"
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
    UpdateSpacecraftInteraction(deltaTime, registry);
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
        
        // 跳过飞船驾驶员（相机由 CameraModeSystem 控制）
        auto* interaction = registry.try_get<components::PlayerSpacecraftInteractionComponent>(entity);
        if (interaction && interaction->isPiloting) {
            continue;
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
        
        // 注意：isGrounded 在上一帧的 move() 之后已设置
        // 如果是第一帧，默认假设在地面（避免初始下坠）
        static bool firstFrame = true;
        if (firstFrame) {
            character.isGrounded = true;
            firstFrame = false;
        }
        
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
        
        // Move the character controller with collision detection
        // 使用默认过滤器，会与所有形状发生碰撞
        physx::PxControllerFilters filters;
        // 设置过滤回调为 nullptr，使用默认行为（与所有物体碰撞）
        filters.mFilterCallback = nullptr;
        filters.mCCTFilterCallback = nullptr;
        // 不设置 mFilterData，使用默认值（与所有物体碰撞）
        
        physx::PxControllerCollisionFlags collisionFlags = character.controller->move(
            displacement, 0.0001f, deltaTime, filters);
        
        // 更新接地状态（基于碰撞标志）
        character.isGrounded = collisionFlags.isSet(physx::PxControllerCollisionFlag::eCOLLISION_DOWN);
        
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
        
        // === 玩家可视模型位置由 SectorSystem.PostPhysicsUpdate 统一处理 ===
        // 不在这里设置，避免坐标系混乱
        // SectorSystem 会将局部坐标转换为世界坐标
        
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

void PlayerSystem::UpdateSpacecraftInteraction(float deltaTime, entt::registry& registry) {
    using namespace DirectX;
    
    // 按键状态（共用一个静态变量避免重复触发）
    static bool fKeyWasPressed = false;
    static float fKeyCooldown = 0.0f;  // 按键冷却时间
    const float KEY_COOLDOWN_TIME = 0.5f;  // 0.5秒冷却
    
    bool fKeyPressed = (GetAsyncKeyState('F') & 0x8000) != 0;
    
    // 更新冷却时间
    if (fKeyCooldown > 0.0f) {
        fKeyCooldown -= deltaTime;
    }
    
    // 查找玩家
    auto playerView = registry.view<PlayerComponent, TransformComponent, PlayerSpacecraftInteractionComponent>();
    
    for (auto playerEntity : playerView) {
        auto& playerTransform = playerView.get<TransformComponent>(playerEntity);
        auto& interaction = playerView.get<PlayerSpacecraftInteractionComponent>(playerEntity);
        
        // 如果正在驾驶，检查退出飞船
        if (interaction.isPiloting) {
            interaction.showInteractionPrompt = false;
            
            // 处理退出飞船（F键）- 需要冷却时间已过
            if (fKeyPressed && !fKeyWasPressed && fKeyCooldown <= 0.0f && interaction.currentSpacecraft != entt::null) {
                std::cout << "[PlayerSystem] F pressed - Exiting spacecraft!" << std::endl;
                
                auto& spacecraft = registry.get<SpacecraftComponent>(interaction.currentSpacecraft);
                auto& spacecraftTransform = registry.get<TransformComponent>(interaction.currentSpacecraft);
                
                // 重置飞船状态
                spacecraft.currentState = SpacecraftComponent::State::IDLE;
                spacecraft.pilot = entt::null;
                
                // 将飞船切换回 Kinematic 模式（保持当前位置）
                if (registry.all_of<RigidBodyComponent>(interaction.currentSpacecraft)) {
                    auto& rb = registry.get<RigidBodyComponent>(interaction.currentSpacecraft);
                    if (rb.physxActor) {
                        auto* dynamicActor = rb.physxActor->is<physx::PxRigidDynamic>();
                        if (dynamicActor) {
                            // 先清除速度
                            dynamicActor->setLinearVelocity(physx::PxVec3(0, 0, 0));
                            dynamicActor->setAngularVelocity(physx::PxVec3(0, 0, 0));
                            // 切换到 Kinematic
                            dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
                            rb.isKinematic = true;
                            std::cout << "[PlayerSystem] Spacecraft switched to Kinematic mode" << std::endl;
                        }
                    }
                }
                
                // 将玩家放到飞船旁边
                if (registry.all_of<components::CharacterControllerComponent>(playerEntity)) {
                    auto& cc = registry.get<components::CharacterControllerComponent>(playerEntity);
                    if (cc.controller) {
                        // 计算飞船的"上"方向（从地球中心指向飞船）
                        XMVECTOR spacecraftPos = XMLoadFloat3(&spacecraftTransform.position);
                        XMVECTOR upDir = XMVector3Normalize(spacecraftPos);
                        
                        // 在飞船旁边偏移一点（沿"上"方向偏移2米）
                        XMVECTOR exitPos = XMVectorAdd(spacecraftPos, XMVectorScale(upDir, 2.0f));
                        XMFLOAT3 exitPosF;
                        XMStoreFloat3(&exitPosF, exitPos);
                        
                        cc.controller->setPosition(physx::PxExtendedVec3(exitPosF.x, exitPosF.y, exitPosF.z));
                        std::cout << "[PlayerSystem] Player moved to exit position: (" 
                                  << exitPosF.x << ", " << exitPosF.y << ", " << exitPosF.z << ")" << std::endl;
                    }
                }
                
                // 重置玩家交互状态
                interaction.isPiloting = false;
                interaction.currentSpacecraft = entt::null;
                
                // 设置冷却时间
                fKeyCooldown = KEY_COOLDOWN_TIME;
            }
            
            fKeyWasPressed = fKeyPressed;
            continue;
        }
        
        // 获取玩家位置（优先使用局部坐标）
        auto* playerInSector = registry.try_get<InSectorComponent>(playerEntity);
        XMFLOAT3 playerPos = playerInSector ? playerInSector->localPosition : playerTransform.position;
        XMVECTOR playerPosVec = XMLoadFloat3(&playerPos);
        
        // 重置交互状态
        interaction.nearestSpacecraft = entt::null;
        interaction.distanceToNearest = FLT_MAX;
        interaction.showInteractionPrompt = false;
        
        // 查找最近的飞船
        auto spacecraftView = registry.view<SpacecraftComponent, TransformComponent>();
        
        for (auto scEntity : spacecraftView) {
            auto& spacecraft = spacecraftView.get<SpacecraftComponent>(scEntity);
            auto& scTransform = spacecraftView.get<TransformComponent>(scEntity);
            
            // 飞船必须是 IDLE 状态才能被登上
            if (spacecraft.currentState != SpacecraftComponent::State::IDLE) {
                continue;
            }
            
            // 计算距离
            float distance = FLT_MAX;
            auto* scInSector = registry.try_get<InSectorComponent>(scEntity);
            
            // 如果玩家和飞船在同一个 Sector，使用局部坐标
            if (playerInSector && scInSector && 
                playerInSector->currentSector == scInSector->currentSector) {
                XMVECTOR scPosVec = XMLoadFloat3(&scInSector->localPosition);
                distance = XMVectorGetX(XMVector3Length(XMVectorSubtract(scPosVec, playerPosVec)));
            } else {
                // 不在同一个 Sector，使用世界坐标
                XMVECTOR scWorldPos = XMLoadFloat3(&scTransform.position);
                XMVECTOR playerWorldPos = XMLoadFloat3(&playerTransform.position);
                distance = XMVectorGetX(XMVector3Length(XMVectorSubtract(scWorldPos, playerWorldPos)));
            }
            
            if (distance < interaction.distanceToNearest) {
                interaction.distanceToNearest = distance;
                interaction.nearestSpacecraft = scEntity;
            }
        }
        
        // 检查是否在交互范围内
        if (interaction.nearestSpacecraft != entt::null) {
            auto& nearestSC = registry.get<SpacecraftComponent>(interaction.nearestSpacecraft);
            if (interaction.distanceToNearest < nearestSC.interactionDistance) {
                interaction.showInteractionPrompt = true;
                
                // 调试输出（每5秒一次）
                static int debugFrame = 0;
                if (++debugFrame % 300 == 0) {
                    std::cout << "[PlayerSystem] Near spacecraft! Distance: " << interaction.distanceToNearest 
                              << "m (range: " << nearestSC.interactionDistance << "m)" << std::endl;
                }
            }
        }
        
        // 处理交互按键（F键）- 需要冷却时间已过
        if (fKeyPressed && !fKeyWasPressed && fKeyCooldown <= 0.0f) {
            if (interaction.showInteractionPrompt && interaction.nearestSpacecraft != entt::null) {
                std::cout << "[PlayerSystem] F pressed - Entering spacecraft!" << std::endl;
                interaction.isPiloting = true;
                interaction.currentSpacecraft = interaction.nearestSpacecraft;
                
                auto& spacecraft = registry.get<SpacecraftComponent>(interaction.nearestSpacecraft);
                spacecraft.pilot = playerEntity;
                spacecraft.currentState = SpacecraftComponent::State::PILOTED;
                
                // 飞船保持 Kinematic 模式（因为我们禁用了物理碰撞 SIMULATION_SHAPE）
                // 飞船控制将通过直接修改位置/旋转来实现
                if (registry.all_of<RigidBodyComponent>(interaction.nearestSpacecraft)) {
                    auto& rb = registry.get<RigidBodyComponent>(interaction.nearestSpacecraft);
                    if (rb.physxActor) {
                        auto* dynamicActor = rb.physxActor->is<physx::PxRigidDynamic>();
                        if (dynamicActor) {
                            // 保持 Kinematic 模式，避免重力影响
                            dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
                            rb.isKinematic = true;
                            
                            std::cout << "[PlayerSystem] Spacecraft ready for piloting (Kinematic mode)!" << std::endl;
                        }
                    }
                }
                
                // 隐藏玩家 - 将玩家移到远离飞船的位置避免碰撞
                if (registry.all_of<components::CharacterControllerComponent>(playerEntity)) {
                    auto& cc = registry.get<components::CharacterControllerComponent>(playerEntity);
                    if (cc.controller) {
                        // 将玩家的 CharacterController 移到远处（不会被渲染，因为相机在飞船上）
                        cc.controller->setPosition(physx::PxExtendedVec3(0, -1000, 0));
                    }
                }
                
                // 设置冷却时间
                fKeyCooldown = KEY_COOLDOWN_TIME;
            }
        }
        
        fKeyWasPressed = fKeyPressed;
    }
}

} // namespace outer_wilds
