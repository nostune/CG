#include "CameraModeSystem.h"
#include "components/CameraComponent.h"
#include "components/FreeCameraComponent.h"
#include "../gameplay/components/PlayerComponent.h"
#include "../gameplay/components/PlayerInputComponent.h"
#include "../gameplay/components/CharacterControllerComponent.h"
#include "../gameplay/components/SpacecraftComponent.h"
#include "../physics/components/GravityAffectedComponent.h"
#include "../physics/components/SectorComponent.h"
#include "../scene/components/TransformComponent.h"
#include "../input/InputManager.h"
#include "../core/DebugManager.h"
#include <Windows.h>

namespace outer_wilds {

using namespace components;

void CameraModeSystem::Initialize() {
    m_CurrentMode = CameraMode::Player;
}

void CameraModeSystem::Initialize(std::shared_ptr<Scene> scene) {
    m_Scene = scene;
    Initialize();
}

void CameraModeSystem::Update(float deltaTime, entt::registry& registry) {
    // 检测玩家飞船驾驶状态变化
    CheckSpacecraftPilotingState(registry);
    CheckModeToggle(registry);
    
    // 注意：飞船模式的相机由 SpacecraftDrivingSystem::UpdateSpacecraftCamera 负责
    // 这里不再调用 UpdateSpacecraftCamera，避免冲突
}

// 检测玩家是否正在驾驶飞船，自动切换相机模式
void CameraModeSystem::CheckSpacecraftPilotingState(entt::registry& registry) {
    auto playerView = registry.view<PlayerComponent, PlayerSpacecraftInteractionComponent>();
    
    for (auto playerEntity : playerView) {
        auto& interaction = playerView.get<PlayerSpacecraftInteractionComponent>(playerEntity);
        
        if (interaction.isPiloting && interaction.currentSpacecraft != entt::null) {
            // 验证飞船实体有效
            if (!registry.valid(interaction.currentSpacecraft)) {
                std::cout << "[CameraMode] WARNING: Invalid spacecraft entity!" << std::endl;
                interaction.isPiloting = false;
                interaction.currentSpacecraft = entt::null;
                continue;
            }
            
            // 玩家正在驾驶飞船，切换到飞船模式
            if (m_CurrentMode != CameraMode::Spacecraft || m_CurrentSpacecraft != interaction.currentSpacecraft) {
                std::cout << "[CameraMode] Switching to spacecraft camera..." << std::endl;
                SwitchToSpacecraftMode(registry, interaction.currentSpacecraft);
            }
        } else {
            // 玩家不在驾驶飞船，如果当前是飞船模式则切换回玩家模式
            if (m_CurrentMode == CameraMode::Spacecraft) {
                std::cout << "[CameraMode] Exiting spacecraft, switching to player camera..." << std::endl;
                SwitchToPlayerMode(registry);
            }
        }
        break;  // 只处理第一个玩家
    }
}

void CameraModeSystem::Shutdown() {
    // 清理
}

void CameraModeSystem::SetCameraMode(CameraMode mode, entt::registry& registry) {
    if (mode == m_CurrentMode) return;
    
    switch (mode) {
        case CameraMode::Player:
            SwitchToPlayerMode(registry);
            break;
        case CameraMode::Free:
            SwitchToFreeMode(registry);
            break;
        case CameraMode::Spacecraft:
            // 飞船模式需要通过 SwitchToSpacecraftMode 设置
            break;
    }
}

void CameraModeSystem::CheckModeToggle(entt::registry& registry) {
    // 检测Shift+ESC组合键
    bool shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    bool escPressed = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;

    // 当两个键都按下且之前至少有一个没按下时，触发切换
    if (shiftPressed && escPressed && (!m_ShiftWasPressed || !m_EscWasPressed)) {
        ToggleCameraMode(registry);
        
        // 输出调试信息
        outer_wilds::DebugManager::GetInstance().Log(
            m_CurrentMode == CameraMode::Player ? 
            "Switched to PLAYER camera mode" : 
            "Switched to FREE camera mode"
        );
    }

    m_ShiftWasPressed = shiftPressed;
    m_EscWasPressed = escPressed;
}

void CameraModeSystem::ToggleCameraMode(entt::registry& registry) {
    if (m_CurrentMode == CameraMode::Player) {
        SwitchToFreeMode(registry);
    } else {
        SwitchToPlayerMode(registry);
    }
}

void CameraModeSystem::SwitchToFreeMode(entt::registry& registry) {
    m_CurrentMode = CameraMode::Free;

    // 查找玩家实体
    auto playerView = registry.view<PlayerComponent, CameraComponent, TransformComponent>();
    for (auto playerEntity : playerView) {
        auto& playerCamera = playerView.get<CameraComponent>(playerEntity);
        auto& playerTransform = playerView.get<TransformComponent>(playerEntity);

        // 停用玩家相机
        playerCamera.isActive = false;

        // 创建或激活自由相机实体
        if (m_FreeCameraEntity == entt::null || !registry.valid(m_FreeCameraEntity)) {
            CreateFreeCameraEntity(registry);
        }

        // 从玩家当前位置初始化自由相机
        auto& freeCamera = registry.get<FreeCameraComponent>(m_FreeCameraEntity);
        auto& freeCameraComp = registry.get<CameraComponent>(m_FreeCameraEntity);
        
        freeCamera.position = playerCamera.position;
        freeCamera.yaw = playerCamera.yaw;
        freeCamera.pitch = playerCamera.pitch;
        freeCamera.isActive = true;
        
        freeCameraComp.isActive = true;

        outer_wilds::DebugManager::GetInstance().Log("CameraMode", "Switched to FREE camera");
        break;
    }
}

void CameraModeSystem::CreateFreeCameraEntity(entt::registry& registry) {
    // 创建自由相机实体
    m_FreeCameraEntity = registry.create();
    
    // 获取玩家当前位置和朝向
    DirectX::XMFLOAT3 playerPos = {0, 0, 0};
    float playerYaw = 0.0f;
    float playerPitch = 0.0f;
    
    // 使用PlayerInputComponent查找玩家实体
    auto playerView = registry.view<PlayerInputComponent, TransformComponent, CameraComponent>();
    for (auto entity : playerView) {
        auto& transform = registry.get<TransformComponent>(entity);
        auto& camera = registry.get<CameraComponent>(entity);
        playerPos = transform.position;
        playerYaw = camera.yaw;
        playerPitch = camera.pitch;
        break;
    }
    
    // 添加Transform - 初始位置从玩家位置创建
    auto& transform = registry.emplace<TransformComponent>(m_FreeCameraEntity);
    transform.position = playerPos;
    transform.rotation = {0, 0, 0, 1};
    
    // 添加CameraComponent
    auto& camera = registry.emplace<CameraComponent>(m_FreeCameraEntity);
    camera.isActive = false;  // 初始为非激活
    camera.fov = 75.0f;
    camera.aspectRatio = 16.0f / 9.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 50000.0f;  // 1百万单位，足够覆盖整个太阳系
    camera.yaw = playerYaw;
    camera.pitch = playerPitch;
    
    // 添加FreeCameraComponent
    auto& freeCamera = registry.emplace<FreeCameraComponent>(m_FreeCameraEntity);
    freeCamera.position = playerPos;
    freeCamera.yaw = playerYaw;
    freeCamera.pitch = playerPitch;
    freeCamera.moveSpeed = 15.0f;
    freeCamera.sprintMultiplier = 3.0f;
    freeCamera.lookSensitivity = 0.003f;
    freeCamera.isActive = false;
    
    outer_wilds::DebugManager::GetInstance().Log("CameraMode", "Free camera entity created");
}

void CameraModeSystem::SwitchToPlayerMode(entt::registry& registry) {
    m_CurrentMode = CameraMode::Player;
    m_CurrentSpacecraft = entt::null;

    // 停用自由相机
    if (m_FreeCameraEntity != entt::null && registry.valid(m_FreeCameraEntity)) {
        auto& freeCamera = registry.get<FreeCameraComponent>(m_FreeCameraEntity);
        auto& freeCameraComp = registry.get<CameraComponent>(m_FreeCameraEntity);
        
        freeCamera.isActive = false;
        freeCameraComp.isActive = false;
    }

    // 查找并激活玩家相机
    auto playerView = registry.view<PlayerComponent, CameraComponent>();
    for (auto playerEntity : playerView) {
        auto& camera = playerView.get<CameraComponent>(playerEntity);
        camera.isActive = true;
        
        outer_wilds::DebugManager::GetInstance().Log("CameraMode", "Switched to PLAYER camera");
        break;
    }
}

void CameraModeSystem::SwitchToSpacecraftMode(entt::registry& registry, entt::entity spacecraft) {
    m_CurrentMode = CameraMode::Spacecraft;
    m_CurrentSpacecraft = spacecraft;
    
    // 停用玩家相机
    auto playerView = registry.view<PlayerComponent, CameraComponent>();
    for (auto playerEntity : playerView) {
        auto& camera = playerView.get<CameraComponent>(playerEntity);
        camera.isActive = false;
    }
    
    // 停用自由相机
    if (m_FreeCameraEntity != entt::null && registry.valid(m_FreeCameraEntity)) {
        auto* freeCamera = registry.try_get<FreeCameraComponent>(m_FreeCameraEntity);
        auto* freeCameraComp = registry.try_get<CameraComponent>(m_FreeCameraEntity);
        
        if (freeCamera) freeCamera->isActive = false;
        if (freeCameraComp) freeCameraComp->isActive = false;
    }
    
    // 获取飞船的 InSectorComponent 来计算正确的相机位置
    auto* spacecraftComp = registry.try_get<SpacecraftComponent>(spacecraft);
    auto* inSector = registry.try_get<InSectorComponent>(spacecraft);
    
    if (spacecraftComp && inSector && inSector->sector != entt::null) {
        auto* sector = registry.try_get<SectorComponent>(inSector->sector);
        if (sector) {
            // 【重要】飞船模型坐标系（此模型上下颠倒，已在初始化时修正）
            // 模型前方 = +Z, 模型上方 = -Y（绕Z旋转180度后）
            DirectX::XMVECTOR localQuat = DirectX::XMLoadFloat4(&inSector->localRotation);
            DirectX::XMMATRIX rotMatrix = DirectX::XMMatrixRotationQuaternion(localQuat);
            
            DirectX::XMVECTOR modelForward = DirectX::XMVector3TransformNormal(
                DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotMatrix);
            DirectX::XMVECTOR modelUp = DirectX::XMVector3TransformNormal(
                DirectX::XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f), rotMatrix);
            
            // 转换到世界坐标系
            DirectX::XMVECTOR sectorRot = DirectX::XMLoadFloat4(&sector->worldRotation);
            DirectX::XMVECTOR worldForward = DirectX::XMVector3Rotate(modelForward, sectorRot);
            DirectX::XMVECTOR worldUp = DirectX::XMVector3Rotate(modelUp, sectorRot);
            
            // 计算飞船世界位置
            DirectX::XMVECTOR localPos = DirectX::XMLoadFloat3(&inSector->localPosition);
            DirectX::XMVECTOR rotatedLocalPos = DirectX::XMVector3Rotate(localPos, sectorRot);
            DirectX::XMVECTOR spacecraftWorldPos = DirectX::XMVectorAdd(
                rotatedLocalPos, DirectX::XMLoadFloat3(&sector->worldPosition));
            
            // 计算相机位置：飞船后上方
            DirectX::XMVECTOR cameraPos = spacecraftWorldPos;
            cameraPos = DirectX::XMVectorSubtract(cameraPos, 
                DirectX::XMVectorScale(worldForward, spacecraftComp->cameraDistance));
            cameraPos = DirectX::XMVectorAdd(cameraPos, 
                DirectX::XMVectorScale(worldUp, spacecraftComp->cameraHeight));
            
            // 计算注视点
            DirectX::XMVECTOR lookTarget = DirectX::XMVectorAdd(
                spacecraftWorldPos, 
                DirectX::XMVectorScale(worldForward, spacecraftComp->cameraLookAheadDistance));
            
            // 更新玩家相机
            auto playerCameraView = registry.view<PlayerComponent, CameraComponent>();
            for (auto playerEntity : playerCameraView) {
                auto* camera = registry.try_get<CameraComponent>(playerEntity);
                if (!camera) continue;
                
                DirectX::XMStoreFloat3(&camera->position, cameraPos);
                DirectX::XMStoreFloat3(&camera->target, lookTarget);
                DirectX::XMStoreFloat3(&camera->up, worldUp);
                camera->isActive = true;
                break;
            }
        }
    }
    
    outer_wilds::DebugManager::GetInstance().Log("CameraMode", "Switched to SPACECRAFT camera");
}

void CameraModeSystem::ExitSpacecraftMode(entt::registry& registry) {
    SwitchToPlayerMode(registry);
}

void CameraModeSystem::UpdateSpacecraftCamera(entt::registry& registry) {
    if (m_CurrentSpacecraft == entt::null || !registry.valid(m_CurrentSpacecraft)) {
        SwitchToPlayerMode(registry);
        return;
    }
    
    auto* spacecraftComp = registry.try_get<SpacecraftComponent>(m_CurrentSpacecraft);
    auto* spacecraftTransform = registry.try_get<TransformComponent>(m_CurrentSpacecraft);
    
    if (!spacecraftComp || !spacecraftTransform) {
        SwitchToPlayerMode(registry);
        return;
    }
    
    // 如果飞船不再被驾驶，退出飞船视角
    if (spacecraftComp->currentState == SpacecraftComponent::State::IDLE) {
        SwitchToPlayerMode(registry);
        return;
    }
    
    // 更新相机位置到飞船位置
    auto playerView = registry.view<PlayerComponent, CameraComponent>();
    for (auto playerEntity : playerView) {
        auto& camera = playerView.get<CameraComponent>(playerEntity);
        
        // 使用飞船的世界坐标（TransformComponent 已被 SectorSystem 更新为世界坐标）
        DirectX::XMVECTOR spacecraftPos = DirectX::XMLoadFloat3(&spacecraftTransform->position);
        
        // 验证四元数有效性
        const auto& rot = spacecraftTransform->rotation;
        float quatLengthSq = rot.x * rot.x + rot.y * rot.y + rot.z * rot.z + rot.w * rot.w;
        DirectX::XMVECTOR spacecraftRot;
        if (quatLengthSq < 0.001f) {
            spacecraftRot = DirectX::XMQuaternionIdentity();
        } else {
            spacecraftRot = DirectX::XMVector4Normalize(DirectX::XMLoadFloat4(&rot));
        }
        
        DirectX::XMVECTOR cameraOffset = DirectX::XMLoadFloat3(&spacecraftComp->cameraOffset);
        
        // 旋转偏移
        cameraOffset = DirectX::XMVector3Rotate(cameraOffset, spacecraftRot);
        DirectX::XMVECTOR cameraPos = DirectX::XMVectorAdd(spacecraftPos, cameraOffset);
        
        DirectX::XMStoreFloat3(&camera.position, cameraPos);
        
        // 飞船模型被绕X轴旋转了-90度，所以：
        // - 原来的Z轴（前）变成了-Y轴
        // - 原来的Y轴（上）变成了Z轴
        // 因此我们需要使用 (0, -1, 0) 作为forward，(0, 0, 1) 作为up
        // 这样经过飞船旋转后就能得到正确的视角方向
        DirectX::XMVECTOR forward = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, -1, 0, 0), spacecraftRot);
        DirectX::XMVECTOR up = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), spacecraftRot);
        
        // 设置相机目标点（前方一定距离）
        DirectX::XMVECTOR targetPos = DirectX::XMVectorAdd(cameraPos, forward);
        DirectX::XMStoreFloat3(&camera.target, targetPos);
        DirectX::XMStoreFloat3(&camera.up, up);
        
        // 同步局部坐标系
        DirectX::XMVECTOR right = DirectX::XMVector3Rotate(DirectX::XMVectorSet(1, 0, 0, 0), spacecraftRot);
        DirectX::XMStoreFloat3(&camera.localUp, up);
        DirectX::XMStoreFloat3(&camera.localForward, forward);
        DirectX::XMStoreFloat3(&camera.localRight, right);
        camera.relativeYaw = 0.0f;
        camera.relativePitch = 0.0f;
        
        camera.isActive = true;
        
        break;
    }
}

} // namespace outer_wilds
