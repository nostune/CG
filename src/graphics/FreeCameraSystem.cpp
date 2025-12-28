#include "FreeCameraSystem.h"
#include "components/CameraComponent.h"
#include "components/FreeCameraComponent.h"
#include "../gameplay/components/PlayerInputComponent.h"
#include "../scene/components/TransformComponent.h"
#include "../input/InputManager.h"
#include "../core/DebugManager.h"
#include <DirectXMath.h>
#include <Windows.h>
#include <sstream>

namespace outer_wilds {

using namespace DirectX;
using namespace components;

void FreeCameraSystem::Initialize() {
    // 初始化自由相机系统
}

void FreeCameraSystem::Initialize(std::shared_ptr<Scene> scene) {
    m_Scene = scene;
    Initialize();
}

void FreeCameraSystem::Update(float deltaTime, entt::registry& registry) {
    // 处理全局按键（ESC+Backspace 解除鼠标锁定，Shift+ESC 退出程序）
    HandleGlobalKeys();
    
    UpdateFreeCameraRotation(deltaTime, registry);
    UpdateFreeCameraMovement(deltaTime, registry);
    SyncToCamera(registry);
}

void FreeCameraSystem::HandleGlobalKeys() {
    static bool escWasPressed = false;
    static bool backspaceWasPressed = false;
    static bool shiftWasPressed = false;
    
    bool escPressed = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    bool backspacePressed = (GetAsyncKeyState(VK_BACK) & 0x8000) != 0;
    bool shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    
    // ESC + Backspace: 切换鼠标锁定
    if (escPressed && backspacePressed && (!escWasPressed || !backspaceWasPressed)) {
        auto& inputMgr = InputManager::GetInstance();
        bool newState = !inputMgr.IsMouseLookEnabled();
        inputMgr.SetMouseLookEnabled(newState);
        DebugManager::GetInstance().Log("FreeCam", newState ? "Mouse look ENABLED" : "Mouse look DISABLED");
    }
    
    // Shift + ESC: 退出程序（发送 WM_CLOSE）
    if (shiftPressed && escPressed && !shiftWasPressed) {
        DebugManager::GetInstance().Log("FreeCam", "Shift+ESC: Requesting exit...");
        PostQuitMessage(0);
    }
    
    escWasPressed = escPressed;
    backspaceWasPressed = backspacePressed;
    shiftWasPressed = shiftPressed;
}

void FreeCameraSystem::Shutdown() {
    // 清理自由相机系统
}

void FreeCameraSystem::UpdateFreeCameraRotation(float deltaTime, entt::registry& registry) {
    auto view = registry.view<FreeCameraComponent, CameraComponent>();
    for (auto entity : view) {
        auto& freeCamera = view.get<FreeCameraComponent>(entity);
        auto& camera = view.get<CameraComponent>(entity);

        // 只在自由相机激活时更新
        if (!freeCamera.isActive) continue;

        // 直接从InputManager获取输入（不依赖PlayerInputComponent）
        auto& inputMgr = InputManager::GetInstance();
        if (!inputMgr.IsMouseLookEnabled()) continue;

        auto& input = inputMgr.GetPlayerInput();

        // 更新旋转（参考SpectatorCamera）
        // lookInput.x = 鼠标水平移动（正值向右）
        // lookInput.y = 鼠标垂直移动（正值向下）
        freeCamera.yaw += input.lookInput.x * freeCamera.lookSensitivity;
        freeCamera.pitch -= input.lookInput.y * freeCamera.lookSensitivity;  // 鼠标下移pitch增加

        // 限制俯仰角防止翻转（89度限制）
        const float maxPitch = XM_PIDIV2 - 0.1f;  // 89度
        const float minPitch = -XM_PIDIV2 + 0.1f; // -89度
        freeCamera.pitch = (std::max)(minPitch, (std::min)(maxPitch, freeCamera.pitch));
    }
}

void FreeCameraSystem::UpdateFreeCameraMovement(float deltaTime, entt::registry& registry) {
    auto view = registry.view<FreeCameraComponent, CameraComponent>();
    
    // 调试：检查是否找到自由相机实体
    static bool logged = false;
    if (!logged) {
        int count = 0;
        for (auto entity : view) {
            auto& freeCamera = view.get<FreeCameraComponent>(entity);
            std::stringstream ss;
            ss << "Found FreeCam entity, isActive=" << freeCamera.isActive;
            DebugManager::GetInstance().Log("FreeCam", ss.str());
            count++;
        }
        DebugManager::GetInstance().Log("FreeCam", "Total FreeCam entities: " + std::to_string(count));
        logged = true;
    }
    
    for (auto entity : view) {
        auto& freeCamera = view.get<FreeCameraComponent>(entity);

        // 只在自由相机激活时更新
        if (!freeCamera.isActive) continue;

        // 直接从InputManager获取输入(自由相机实体没有PlayerInputComponent)
        auto& inputMgr = InputManager::GetInstance();
        auto& input = inputMgr.GetPlayerInput();
        
        // 调试输出 - 检查键盘直接状态
        static int debugFrame = 0;
        if (++debugFrame % 60 == 0) {
            bool wPressed = (GetAsyncKeyState('W') & 0x8000) != 0;
            bool sPressed = (GetAsyncKeyState('S') & 0x8000) != 0;
            bool aPressed = (GetAsyncKeyState('A') & 0x8000) != 0;
            bool dPressed = (GetAsyncKeyState('D') & 0x8000) != 0;
            
            std::stringstream ss;
            ss << "Keys: W=" << wPressed << " S=" << sPressed << " A=" << aPressed << " D=" << dPressed
               << " | Input: x=" << input.moveInput.x << " y=" << input.moveInput.y
               << " | Pos: (" << freeCamera.position.x << "," << freeCamera.position.y << "," << freeCamera.position.z << ")";
            DebugManager::GetInstance().Log("FreeCam", ss.str());
        }

        // 从yaw和pitch计算forward向量（参考SpectatorCamera.updateVectors）
        // 左手坐标系：X右，Y上，Z前
        XMVECTOR forward = XMVectorSet(
            sinf(freeCamera.yaw) * cosf(freeCamera.pitch),   // X分量
            sinf(freeCamera.pitch),                          // Y分量
            cosf(freeCamera.yaw) * cosf(freeCamera.pitch),   // Z分量
            0.0f
        );
        forward = XMVector3Normalize(forward);

        // 计算right向量：worldUp × forward（左手坐标系叉乘）
        XMVECTOR worldUp = XMVectorSet(0, 1, 0, 0);
        XMVECTOR right = XMVector3Cross(worldUp, forward);
        right = XMVector3Normalize(right);

        // 计算移动速度（Shift加速）
        float currentSpeed = freeCamera.moveSpeed;
        if (input.sprintHeld) {
            currentSpeed *= freeCamera.sprintMultiplier;
        }

        // WASD移动（参考SpectatorCamera - forward/right/up独立控制）
        XMVECTOR movement = XMVectorZero();
        
        // W/S: 沿forward方向移动
        movement = XMVectorAdd(movement, XMVectorScale(forward, input.moveInput.y * currentSpeed * deltaTime));
        
        // A/D: 沿right方向移动  
        movement = XMVectorAdd(movement, XMVectorScale(right, input.moveInput.x * currentSpeed * deltaTime));

        // Space上升，Ctrl下降（沿世界Y轴）
        bool spacePressed = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        
        if (spacePressed) {
            movement = XMVectorAdd(movement, XMVectorScale(worldUp, currentSpeed * deltaTime));
        }
        if (ctrlPressed) {
            movement = XMVectorSubtract(movement, XMVectorScale(worldUp, currentSpeed * deltaTime));
        }

        // 注意：不要归一化！因为我们已经在上面分别乘以了deltaTime和speed

        // 应用移动
        XMVECTOR pos = XMLoadFloat3(&freeCamera.position);
        pos = XMVectorAdd(pos, movement);
        XMStoreFloat3(&freeCamera.position, pos);
    }
}

void FreeCameraSystem::SyncToCamera(entt::registry& registry) {
    auto view = registry.view<FreeCameraComponent, CameraComponent, TransformComponent>();
    for (auto entity : view) {
        auto& freeCamera = view.get<FreeCameraComponent>(entity);
        auto& camera = view.get<CameraComponent>(entity);
        auto& transform = view.get<TransformComponent>(entity);

        // 只在自由相机激活时同步
        if (!freeCamera.isActive) continue;

        // 更新相机位置 - 同时更新CameraComponent和TransformComponent
        camera.position = freeCamera.position;
        transform.position = freeCamera.position;

        // 计算相机目标点（使用相同的forward计算方式）
        XMVECTOR forward = XMVectorSet(
            sinf(freeCamera.yaw) * cosf(freeCamera.pitch),
            sinf(freeCamera.pitch),
            cosf(freeCamera.yaw) * cosf(freeCamera.pitch),
            0.0f
        );
        forward = XMVector3Normalize(forward);

        XMVECTOR pos = XMLoadFloat3(&camera.position);
        XMVECTOR target = XMVectorAdd(pos, forward);
        XMStoreFloat3(&camera.target, target);

        // 相机上方向（世界空间Y轴）
        camera.up = {0.0f, 1.0f, 0.0f};
        
        // 同步yaw/pitch到CameraComponent（用于渲染系统）
        camera.yaw = freeCamera.yaw;
        camera.pitch = freeCamera.pitch;
    }
}

} // namespace outer_wilds
