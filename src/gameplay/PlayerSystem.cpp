/**
 * PlayerSystem.cpp
 * 
 * 玩家控制系统 - 支持星球表面行走
 * 
 * 核心逻辑：
 * 1. 根据重力方向动态调整玩家"上方向"
 * 2. 移动方向相对于当前localUp/localForward计算
 * 3. 通过PhysX刚体控制移动
 */

#include "PlayerSystem.h"
#include "../scene/components/TransformComponent.h"
#include "../scene/components/ChildEntityComponent.h"
#include "components/PlayerComponent.h"
#include "components/PlayerInputComponent.h"
#include "components/CharacterControllerComponent.h"
#include "components/SpacecraftComponent.h"
#include "../graphics/components/CameraComponent.h"
#include "../graphics/components/FreeCameraComponent.h"
#include "../graphics/components/MeshComponent.h"
#include "../physics/components/SectorComponent.h"
#include "../physics/components/GravityAffectedComponent.h"
#include "../physics/components/RigidBodyComponent.h"
#include "../physics/PhysXManager.h"
#include "../input/InputManager.h"
#include "../core/DebugManager.h"
#include <DirectXMath.h>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace outer_wilds {

using namespace components;
using namespace DirectX;

void PlayerSystem::Initialize() {
    std::cout << "[PlayerSystem] Initialized with surface walking support" << std::endl;
}

void PlayerSystem::Initialize(std::shared_ptr<Scene> scene) {
    m_Scene = scene;
    Initialize();
}

void PlayerSystem::Update(float deltaTime, entt::registry& registry) {
    ProcessPlayerInput(deltaTime, registry);
    UpdateSpacecraftInteraction(deltaTime, registry);
    UpdateSurfaceWalking(deltaTime, registry);
    UpdatePlayerCamera(deltaTime, registry);
}

void PlayerSystem::Shutdown() {
    // Cleanup player system
}

void PlayerSystem::ProcessPlayerInput(float deltaTime, entt::registry& registry) {
    InputManager::GetInstance().Update();

    // 如果自由相机激活，则屏蔽玩家输入，避免干涉玩家实体
    bool freeCameraActive = false;
    {
        auto freeCamView = registry.view<FreeCameraComponent>();
        for (auto entity : freeCamView) {
            auto& freeCam = freeCamView.get<FreeCameraComponent>(entity);
            if (freeCam.isActive) {
                freeCameraActive = true;
                break;
            }
        }
    }

    // 处理角色控制器输入
    auto view = registry.view<CharacterControllerComponent, PlayerInputComponent>();
    for (auto entity : view) {
        auto& character = view.get<CharacterControllerComponent>(entity);
        auto& playerInput = view.get<PlayerInputComponent>(entity);

        // 当自由相机激活时，清空玩家输入，保持玩家静止
        if (freeCameraActive) {
            playerInput = {};
            character.forwardInput = 0.0f;
            character.rightInput = 0.0f;
            character.wantsToJump = false;
            character.wantsToRun = false;
            continue;
        }

        // 当玩家正在驾驶飞船时，暂停角色输入，防止在地面乱动
        if (auto* interaction = registry.try_get<PlayerSpacecraftInteractionComponent>(entity)) {
            if (interaction->isPiloting) {
                playerInput = {};
                character.forwardInput = 0.0f;
                character.rightInput = 0.0f;
                character.wantsToJump = false;
                character.wantsToRun = false;
                continue;
            }
        }

        playerInput = InputManager::GetInstance().GetPlayerInput();
        
        // 映射输入
        character.forwardInput = playerInput.moveInput.y;   // W/S
        character.rightInput = playerInput.moveInput.x;     // A/D
        character.wantsToJump = playerInput.jumpPressed;
        character.wantsToRun = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        
        // 鼠标视角旋转
        if (playerInput.mouseLookEnabled) {
            character.cameraYaw += playerInput.lookInput.x * 0.003f;
            character.cameraPitch += playerInput.lookInput.y * 0.003f;
            
            // 限制俯仰角
            const float maxPitch = XM_PIDIV2 - 0.1f;
            character.cameraPitch = std::clamp(character.cameraPitch, -maxPitch, maxPitch);
        }
    }
}

/**
 * 更新局部坐标系（基于重力方向）
 * localUp = -gravityDir（头顶方向）
 * localForward/localRight 在切平面上，用于计算移动方向
 */
void PlayerSystem::UpdateLocalCoordinateFrame(XMFLOAT3& localUp, XMFLOAT3& localForward, 
                                               XMFLOAT3& localRight, const XMFLOAT3& gravityDir) {
    // localUp = -gravityDir（头顶方向，垂直于切平面）
    XMVECTOR up = XMVector3Normalize(XMVectorNegate(XMLoadFloat3(&gravityDir)));
    XMStoreFloat3(&localUp, up);
    
    // 保持旧的forward方向的切向分量
    XMVECTOR oldForward = XMLoadFloat3(&localForward);
    float oldForwardLen = XMVectorGetX(XMVector3Length(oldForward));
    
    // 如果旧forward无效，使用世界坐标初始化
    if (oldForwardLen < 0.5f) {
        // 选择与up最不平行的参考向量
        XMVECTOR worldZ = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        XMVECTOR worldX = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
        
        float dotZ = std::abs(XMVectorGetX(XMVector3Dot(up, worldZ)));
        XMVECTOR reference = (dotZ < 0.9f) ? worldZ : worldX;
        
        // right = up × reference
        XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, reference));
        // forward = right × up
        XMVECTOR forward = XMVector3Normalize(XMVector3Cross(right, up));
        
        XMStoreFloat3(&localRight, right);
        XMStoreFloat3(&localForward, forward);
        return;
    }
    
    // 将旧forward投影到新的切平面上
    // forward' = forward - (forward · up) * up
    float dotFwdUp = XMVectorGetX(XMVector3Dot(oldForward, up));
    XMVECTOR projForward = XMVectorSubtract(oldForward, XMVectorScale(up, dotFwdUp));
    
    float projLen = XMVectorGetX(XMVector3Length(projForward));
    if (projLen < 0.001f) {
        // forward与up平行，需要重新选择
        XMVECTOR worldZ = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        XMVECTOR worldX = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
        float dotZ = std::abs(XMVectorGetX(XMVector3Dot(up, worldZ)));
        XMVECTOR reference = (dotZ < 0.9f) ? worldZ : worldX;
        projForward = XMVectorSubtract(reference, XMVectorScale(up, XMVectorGetX(XMVector3Dot(reference, up))));
    }
    
    XMVECTOR forward = XMVector3Normalize(projForward);
    // right = up × forward (确保右手坐标系)
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, forward));
    // 重新计算forward确保正交
    forward = XMVector3Normalize(XMVector3Cross(right, up));
    
    XMStoreFloat3(&localForward, forward);
    XMStoreFloat3(&localRight, right);
}

/**
 * 地面射线检测
 */
bool PlayerSystem::GroundRaycast(const XMFLOAT3& origin, const XMFLOAT3& dir, 
                                  float maxDist, XMFLOAT3& hitPoint, XMFLOAT3& hitNormal) {
    auto* pxScene = PhysXManager::GetInstance().GetScene();
    if (!pxScene) return false;
    
    physx::PxVec3 pxOrigin(origin.x, origin.y, origin.z);
    physx::PxVec3 pxDir(dir.x, dir.y, dir.z);
    pxDir.normalize();
    
    physx::PxRaycastBuffer hit;
    if (pxScene->raycast(pxOrigin, pxDir, maxDist, hit, 
                         physx::PxHitFlag::ePOSITION | physx::PxHitFlag::eNORMAL)) {
        hitPoint = { hit.block.position.x, hit.block.position.y, hit.block.position.z };
        hitNormal = { hit.block.normal.x, hit.block.normal.y, hit.block.normal.z };
        return true;
    }
    return false;
}

/**
 * 表面行走更新
 * 使用 PxController 的 move 方法和 setUpDirection 实现球面行走
 * - 每帧更新 upDirection 为 -gravityDir
 * - PxController 会自动处理坡度检测
 */
void PlayerSystem::UpdateSurfaceWalking(float deltaTime, entt::registry& registry) {
    auto view = registry.view<CharacterControllerComponent, GravityAffectedComponent, 
                              InSectorComponent, TransformComponent>();
    
    for (auto entity : view) {
        auto& character = view.get<CharacterControllerComponent>(entity);
        auto& gravity = view.get<GravityAffectedComponent>(entity);
        auto& inSector = view.get<InSectorComponent>(entity);
        auto& transform = view.get<TransformComponent>(entity);

        // 正在驾驶飞船时，不更新地面行走逻辑
        if (auto* interaction = registry.try_get<PlayerSpacecraftInteractionComponent>(entity)) {
            if (interaction->isPiloting) {
                continue;
            }
        }

        if (!character.pxController) continue;
        
        // Debug: 第一帧详细检查
        static bool firstFrame = true;
        static int frameCount = 0;
        frameCount++;
        

        
        // 1. 更新局部坐标系（基于重力方向）
        UpdateLocalCoordinateFrame(character.localUp, character.localForward, 
                                   character.localRight, gravity.currentGravityDir);
        
        // 2. 关键：设置 PxController 的 up 方向
        // 这让 PhysX 知道什么是"上"，从而正确处理坡度
        physx::PxVec3 pxUp(character.localUp.x, character.localUp.y, character.localUp.z);
        character.pxController->setUpDirection(pxUp);
        
        // 3. 获取当前位置
        physx::PxExtendedVec3 pxPos = character.pxController->getPosition();
        XMFLOAT3 currentPos = { (float)pxPos.x, (float)pxPos.y, (float)pxPos.z };
        
        // 4. 计算移动方向（相对于视角yaw和局部坐标系）
        XMVECTOR up = XMLoadFloat3(&character.localUp);
        XMVECTOR fwd = XMLoadFloat3(&character.localForward);
        XMVECTOR right = XMLoadFloat3(&character.localRight);
        
        // 绕localUp旋转forward以应用yaw
        XMMATRIX yawRotation = XMMatrixRotationAxis(up, character.cameraYaw);
        XMVECTOR rotatedForward = XMVector3TransformNormal(fwd, yawRotation);
        XMVECTOR rotatedRight = XMVector3TransformNormal(right, yawRotation);
        
        // 5. 计算移动向量（在切平面上）
        float speed = character.wantsToRun ? character.runSpeed : character.moveSpeed;
        
        XMVECTOR moveDir = XMVectorAdd(
            XMVectorScale(rotatedForward, character.forwardInput),
            XMVectorScale(rotatedRight, character.rightInput)
        );
        
        float inputMagnitude = XMVectorGetX(XMVector3Length(moveDir));
        if (inputMagnitude > 0.01f) {
            moveDir = XMVector3Normalize(moveDir);
        }
        
        // 水平移动量
        XMVECTOR horizontalMove = XMVectorScale(moveDir, speed * inputMagnitude * deltaTime);
        
        // 6. 处理重力和跳跃
        XMVECTOR vel = XMLoadFloat3(&character.velocity);
        
        // 使用上一帧的grounded状态来决定当前帧的行为
        if (character.isGrounded) {
            // 在地面上：重置垂直速度
            // 将速度投影到切平面
            float velDotUp = XMVectorGetX(XMVector3Dot(vel, up));
            if (velDotUp < 0) {  // 只清除向下的速度
                vel = XMVectorSubtract(vel, XMVectorScale(up, velDotUp));
            }
            
            // 跳跃
            if (character.wantsToJump) {
                vel = XMVectorAdd(vel, XMVectorScale(up, character.jumpForce));
                character.isGrounded = false;
            }
        } else {
            // 空中：应用重力
            float gravityAccel = gravity.currentGravityStrength;
            XMVECTOR gravityDir = XMLoadFloat3(&gravity.currentGravityDir);
            vel = XMVectorAdd(vel, XMVectorScale(gravityDir, gravityAccel * deltaTime));
            
            // 空中控制（较弱）
            horizontalMove = XMVectorScale(horizontalMove, character.airControl);
        }
        
        XMStoreFloat3(&character.velocity, vel);
        
        // 7. 组合移动向量（水平 + 垂直）
        XMVECTOR verticalMove = XMVectorScale(vel, deltaTime);
        XMVECTOR totalMove = XMVectorAdd(horizontalMove, verticalMove);
        
        XMFLOAT3 moveDelta;
        XMStoreFloat3(&moveDelta, totalMove);
        
        // 8. 调用 PxController::move
        // minDist 设为 0.01f 防止微小移动导致的穿透问题
        physx::PxVec3 displacement(moveDelta.x, moveDelta.y, moveDelta.z);
        physx::PxControllerFilters filters;
        physx::PxControllerCollisionFlags collisionFlags = character.pxController->move(displacement, 0.01f, deltaTime, filters);

        
        // 检测碰撞标志，更新grounded状态
        character.wasGrounded = character.isGrounded;
        character.isGrounded = (collisionFlags & physx::PxControllerCollisionFlag::eCOLLISION_DOWN) != physx::PxControllerCollisionFlags(0);
        
        // Debug: 每60帧打印碰撞状态

        // 如果检测到向下碰撞且在空中，清除垂直速度（着陆）
        if (character.isGrounded && !character.wasGrounded) {
            // 将速度投影到切平面，移除垂直分量
            float velDotUp = XMVectorGetX(XMVector3Dot(vel, up));
            if (velDotUp < 0) {  // 只有在下落时才清除
                vel = XMVectorSubtract(vel, XMVectorScale(up, velDotUp));
                XMStoreFloat3(&character.velocity, vel);
            }
        }
        
        // 9. 获取新位置并更新 InSectorComponent
        // 【重要】只更新 inSector.localPosition，不要写 transform.position！
        // transform.position 会由 SectorPhysicsSystem 根据 localPosition 自动计算世界坐标
        pxPos = character.pxController->getPosition();
        inSector.localPosition = { (float)pxPos.x, (float)pxPos.y, (float)pxPos.z };
        
        // 10. 计算角色旋转（使角色"站立"朝向localUp，面向前进方向）
        // charUp = 重力反方向（脚朝向地面）
        // charForward = 玩家视角前方（投影到切平面）
        XMVECTOR charUp = XMVector3Normalize(up);
        XMVECTOR charForward = XMVector3Normalize(rotatedForward);
        
        // 确保 forward 和 up 正交
        XMVECTOR charRight = XMVector3Normalize(XMVector3Cross(charUp, charForward));
        charForward = XMVector3Normalize(XMVector3Cross(charRight, charUp));
        
        // 构建正交旋转矩阵（列向量：X=right, Y=up, Z=forward）
        // DirectXMath 使用行主序，所以 r[0] 是第一行
        XMMATRIX rotMatrix;
        rotMatrix.r[0] = XMVectorSetW(charRight, 0.0f);
        rotMatrix.r[1] = XMVectorSetW(charUp, 0.0f);
        rotMatrix.r[2] = XMVectorSetW(charForward, 0.0f);
        rotMatrix.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
        
        XMVECTOR quat = XMQuaternionRotationMatrix(rotMatrix);
        
        // 【重要】更新 inSector.localRotation 而不是 transform.rotation
        // 因为 SectorPhysicsSystem 会用 localRotation 来计算世界 rotation
        XMStoreFloat4(&inSector.localRotation, quat);

    }
}

void PlayerSystem::UpdatePlayerCamera(float deltaTime, entt::registry& registry) {
    // 更新跟随玩家的相机
    auto playerView = registry.view<CharacterControllerComponent, TransformComponent, InSectorComponent>();
    
    for (auto playerEntity : playerView) {
        auto& character = playerView.get<CharacterControllerComponent>(playerEntity);
        auto& playerTransform = playerView.get<TransformComponent>(playerEntity);
        auto& inSector = playerView.get<InSectorComponent>(playerEntity);

        // 驾驶飞船时，玩家相机由 CameraModeSystem/SpacecraftDrivingSystem 管理
        if (auto* interaction = registry.try_get<PlayerSpacecraftInteractionComponent>(playerEntity)) {
            if (interaction->isPiloting) {
                continue;
            }
        }
        
        // 获取扇区世界位置
        XMFLOAT3 sectorWorldPos = { 0.0f, 0.0f, 0.0f };
        XMFLOAT4 sectorWorldRot = { 0.0f, 0.0f, 0.0f, 1.0f };
        if (inSector.sector != entt::null) {
            auto* sector = registry.try_get<SectorComponent>(inSector.sector);
            if (sector) {
                sectorWorldPos = sector->worldPosition;
                sectorWorldRot = sector->worldRotation;
            }
        }
        
        // 计算玩家世界位置（局部坐标 + 扇区世界位置）
        XMVECTOR localPos = XMLoadFloat3(&inSector.localPosition);
        XMVECTOR sectorPos = XMLoadFloat3(&sectorWorldPos);
        XMVECTOR sectorRotQuat = XMLoadFloat4(&sectorWorldRot);
        
        // 如果扇区有旋转，先旋转局部位置
        XMVECTOR rotatedLocalPos = XMVector3Rotate(localPos, sectorRotQuat);
        XMVECTOR playerWorldPos = XMVectorAdd(rotatedLocalPos, sectorPos);
        
        // 查找相机
        auto cameraView = registry.view<CameraComponent, TransformComponent>();
        for (auto camEntity : cameraView) {
            // 跳过自由相机模式
            auto* freeCamera = registry.try_get<FreeCameraComponent>(camEntity);
            if (freeCamera && freeCamera->isActive) continue;
            
            auto& camera = cameraView.get<CameraComponent>(camEntity);
            auto& camTransform = cameraView.get<TransformComponent>(camEntity);
            
            // 计算相机位置（玩家世界位置 + 基于localUp的偏移）
            XMVECTOR localUp = XMLoadFloat3(&character.localUp);
            
            // 如果扇区有旋转，localUp 也需要旋转到世界坐标系
            XMVECTOR worldUp = XMVector3Rotate(localUp, sectorRotQuat);
            
            // 相机在玩家眼睛位置
            // playerWorldPos 是胶囊中心位置，但模型原点在脚底
            // 所以 playerWorldPos 实际上是模型脚底位置
            // cameraOffset.y 是眼睛距离脚底的高度，直接使用
            XMVECTOR camPos = XMVectorAdd(playerWorldPos, XMVectorScale(worldUp, character.cameraOffset.y));
            XMStoreFloat3(&camTransform.position, camPos);
            XMStoreFloat3(&camera.position, camPos);
            
            // 计算相机朝向（基于yaw和pitch）
            XMVECTOR fwd = XMLoadFloat3(&character.localForward);
            XMVECTOR right = XMLoadFloat3(&character.localRight);
            
            // 如果扇区有旋转，forward 和 right 也需要旋转到世界坐标系
            fwd = XMVector3Rotate(fwd, sectorRotQuat);
            right = XMVector3Rotate(right, sectorRotQuat);
            localUp = worldUp;
            
            // 绕localUp旋转（yaw）
            XMMATRIX yawRot = XMMatrixRotationAxis(localUp, character.cameraYaw);
            fwd = XMVector3TransformNormal(fwd, yawRot);
            right = XMVector3TransformNormal(right, yawRot);
            
            // 绕right旋转（pitch）
            XMMATRIX pitchRot = XMMatrixRotationAxis(right, character.cameraPitch);
            fwd = XMVector3TransformNormal(fwd, pitchRot);
            
            // 相机目标点
            XMVECTOR target = XMVectorAdd(camPos, fwd);
            XMStoreFloat3(&camera.target, target);
            
            // 相机up方向
            XMStoreFloat3(&camera.up, localUp);
        }
    }
}

entt::entity PlayerSystem::FindPlayerCamera(entt::entity playerEntity, entt::registry& registry) {
    auto cameraView = registry.view<CameraComponent>();
    if (!cameraView.empty()) {
        return *cameraView.begin();
    }
    return entt::null;
}

entt::entity PlayerSystem::FindCameraPlayer(entt::entity cameraEntity, entt::registry& registry) {
    auto playerView = registry.view<PlayerInputComponent>();
    if (!playerView.empty()) {
        return *playerView.begin();
    }
    return entt::null;
}

void PlayerSystem::UpdateSpacecraftInteraction(float deltaTime, entt::registry& registry) {
    using namespace DirectX;
    
    // 按键状态
    static bool fKeyWasPressed = false;
    static float fKeyCooldown = 0.0f;
    const float KEY_COOLDOWN_TIME = 0.5f;
    
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
            
            // 处理退出飞船（F键）
            if (fKeyPressed && !fKeyWasPressed && fKeyCooldown <= 0.0f && interaction.currentSpacecraft != entt::null) {
                std::cout << "[PlayerSystem] F pressed - Exiting spacecraft!" << std::endl;
                
                entt::entity spacecraftEntity = interaction.currentSpacecraft;
                auto& spacecraft = registry.get<SpacecraftComponent>(spacecraftEntity);

                // 计算下船后玩家在扇区内的新位置（基于飞船局部坐标与 exitOffset）
                auto* spacecraftInSector = registry.try_get<InSectorComponent>(spacecraftEntity);
                auto* playerInSector = registry.try_get<InSectorComponent>(playerEntity);
                auto* character = registry.try_get<CharacterControllerComponent>(playerEntity);
                if (spacecraftInSector && playerInSector) {
                    XMVECTOR scPos = XMLoadFloat3(&spacecraftInSector->localPosition);
                    XMVECTOR scRotQuat = XMLoadFloat4(&spacecraftInSector->localRotation);
                    XMMATRIX scRot = XMMatrixRotationQuaternion(scRotQuat);

                    // 飞船局部坐标轴（与 SpacecraftDrivingSystem 保持一致）
                    XMVECTOR fwd = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), scRot);
                    XMVECTOR right = XMVector3TransformNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), scRot);
                    XMVECTOR up = XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), scRot);

                    XMVECTOR offset = XMVectorZero();
                    offset = XMVectorAdd(offset, XMVectorScale(right, spacecraft.exitOffset.x));
                    offset = XMVectorAdd(offset, XMVectorScale(up, spacecraft.exitOffset.y));
                    offset = XMVectorAdd(offset, XMVectorScale(fwd, spacecraft.exitOffset.z));

                    XMVECTOR newLocalPos = XMVectorAdd(scPos, offset);
                    XMStoreFloat3(&playerInSector->localPosition, newLocalPos);
                    playerInSector->sector = spacecraftInSector->sector;
                    playerInSector->isInitialized = true;
                    playerInSector->needsSync = true;

                    // 同步 PhysX 胶囊控制器位置并清空速度
                    if (character && character->pxController) {
                        physx::PxExtendedVec3 newPxPos(
                            playerInSector->localPosition.x,
                            playerInSector->localPosition.y,
                            playerInSector->localPosition.z
                        );
                        character->pxController->setPosition(newPxPos);
                        character->velocity = { 0.0f, 0.0f, 0.0f };
                        character->isGrounded = false;
                        character->wasGrounded = false;
                    }
                }

                // 重新显示玩家模型（主实体及其子网格）
                if (auto* mesh = registry.try_get<MeshComponent>(playerEntity)) {
                    mesh->isVisible = true;
                }
                auto childView = registry.view<ChildEntityComponent, MeshComponent>();
                for (auto childEntity : childView) {
                    auto& child = childView.get<ChildEntityComponent>(childEntity);
                    if (child.parent == playerEntity) {
                        auto& childMesh = childView.get<MeshComponent>(childEntity);
                        childMesh.isVisible = true;
                    }
                }

                // 重置飞船状态
                spacecraft.currentState = SpacecraftComponent::State::IDLE;
                spacecraft.pilot = entt::null;
                
                // 重置玩家交互状态
                interaction.isPiloting = false;
                interaction.currentSpacecraft = entt::null;
                
                fKeyCooldown = KEY_COOLDOWN_TIME;
            }
            
            fKeyWasPressed = fKeyPressed;
            continue;
        }
        
        // 获取玩家位置
        XMFLOAT3 playerPos = playerTransform.position;
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
            
            // 计算距离（使用世界坐标）
            XMVECTOR scWorldPos = XMLoadFloat3(&scTransform.position);
            float distance = XMVectorGetX(XMVector3Length(XMVectorSubtract(scWorldPos, playerPosVec)));
            
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
            }
        }
        
        // 处理交互按键（F键）
        if (fKeyPressed && !fKeyWasPressed && fKeyCooldown <= 0.0f) {
            if (interaction.showInteractionPrompt && interaction.nearestSpacecraft != entt::null) {
                std::cout << "[PlayerSystem] F pressed - Entering spacecraft!" << std::endl;
                interaction.isPiloting = true;
                interaction.currentSpacecraft = interaction.nearestSpacecraft;
                
                auto& spacecraft = registry.get<SpacecraftComponent>(interaction.nearestSpacecraft);
                spacecraft.pilot = playerEntity;
                spacecraft.currentState = SpacecraftComponent::State::PILOTED;

                 // 进入飞船时，隐藏玩家模型（主实体及其子网格）
                 if (auto* mesh = registry.try_get<MeshComponent>(playerEntity)) {
                     mesh->isVisible = false;
                 }
                 auto childView = registry.view<ChildEntityComponent, MeshComponent>();
                 for (auto childEntity : childView) {
                     auto& child = childView.get<ChildEntityComponent>(childEntity);
                     if (child.parent == playerEntity) {
                         auto& childMesh = childView.get<MeshComponent>(childEntity);
                         childMesh.isVisible = false;
                     }
                 }
                
                // 【关键】唤醒 PhysX actor，确保物理正常工作
                auto* rigidBody = registry.try_get<RigidBodyComponent>(interaction.nearestSpacecraft);
                if (rigidBody && rigidBody->physxActor) {
                    auto* dynamicActor = rigidBody->physxActor->is<physx::PxRigidDynamic>();
                    if (dynamicActor) {
                        dynamicActor->wakeUp();
                        std::cout << "[PlayerSystem] Spacecraft PhysX actor woken up!" << std::endl;
                    }
                }
                
                fKeyCooldown = KEY_COOLDOWN_TIME;
            }
        }
        
        fKeyWasPressed = fKeyPressed;
    }
}

} // namespace outer_wilds
