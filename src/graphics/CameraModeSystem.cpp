#include "CameraModeSystem.h"
#include "components/CameraComponent.h"
#include "components/FreeCameraComponent.h"
#include "../gameplay/components/PlayerComponent.h"
#include "../gameplay/components/PlayerInputComponent.h"
#include "../gameplay/components/CharacterControllerComponent.h"
#include "../gameplay/components/SpacecraftComponent.h"
#include "../physics/components/GravityAffectedComponent.h"
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
    CheckModeToggle(registry);
    
    // 如果在飞船模式，更新飞船相机
    if (m_CurrentMode == CameraMode::Spacecraft) {
        UpdateSpacecraftCamera(registry);
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
    camera.farPlane = 1000.0f;
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
    
    // 获取飞船并设置相机
    auto* spacecraftComp = registry.try_get<SpacecraftComponent>(spacecraft);
    auto* spacecraftTransform = registry.try_get<TransformComponent>(spacecraft);
    
    if (spacecraftComp && spacecraftTransform) {
        // 查找玩家相机并更新其位置到飞船位置
        for (auto playerEntity : playerView) {
            auto& camera = playerView.get<CameraComponent>(playerEntity);
            
            // 计算飞船内部相机位置
            DirectX::XMVECTOR spacecraftPos = DirectX::XMLoadFloat3(&spacecraftTransform->position);
            DirectX::XMVECTOR spacecraftRot = DirectX::XMLoadFloat4(&spacecraftTransform->rotation);
            DirectX::XMVECTOR cameraOffset = DirectX::XMLoadFloat3(&spacecraftComp->cameraOffset);
            
            // 旋转偏移
            cameraOffset = DirectX::XMVector3Rotate(cameraOffset, spacecraftRot);
            DirectX::XMVECTOR cameraPos = DirectX::XMVectorAdd(spacecraftPos, cameraOffset);
            
            DirectX::XMStoreFloat3(&camera.position, cameraPos);
            camera.yaw = spacecraftComp->yaw;
            camera.pitch = spacecraftComp->pitch;
            camera.isActive = true;
            
            break;
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
        
        // 计算飞船内部相机位置
        DirectX::XMVECTOR spacecraftPos = DirectX::XMLoadFloat3(&spacecraftTransform->position);
        DirectX::XMVECTOR spacecraftRot = DirectX::XMLoadFloat4(&spacecraftTransform->rotation);
        DirectX::XMVECTOR cameraOffset = DirectX::XMLoadFloat3(&spacecraftComp->cameraOffset);
        
        // 旋转偏移
        cameraOffset = DirectX::XMVector3Rotate(cameraOffset, spacecraftRot);
        DirectX::XMVECTOR cameraPos = DirectX::XMVectorAdd(spacecraftPos, cameraOffset);
        
        DirectX::XMStoreFloat3(&camera.position, cameraPos);
        
        // 直接使用飞船的旋转四元数计算视角方向
        // 飞船前方向就是相机看向的方向
        DirectX::XMVECTOR forward = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), spacecraftRot);
        DirectX::XMVECTOR up = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 1, 0, 0), spacecraftRot);
        
        // 设置相机目标点（前方一定距离）
        DirectX::XMVECTOR targetPos = DirectX::XMVectorAdd(cameraPos, forward);
        DirectX::XMStoreFloat3(&camera.target, targetPos);
        DirectX::XMStoreFloat3(&camera.up, up);
        
        // 同步局部坐标系（用于球面重力系统兼容）
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
