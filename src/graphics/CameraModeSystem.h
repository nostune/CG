#pragma once
#include "../core/ECS.h"
#include "../scene/Scene.h"
#include "CameraMode.h"
#include <entt/entt.hpp>
#include <memory>

namespace outer_wilds {

/// <summary>
/// 相机模式管理系统
/// 处理玩家视角、飞船视角和自由视角之间的切换
/// 按Shift+ESC切换到自由模式
/// 上下飞船时自动切换相机模式
/// </summary>
class CameraModeSystem : public System {
public:
    void Initialize() override;
    void Initialize(std::shared_ptr<Scene> scene);
    void Update(float deltaTime, entt::registry& registry) override;
    void Shutdown() override;

    /// <summary>
    /// 获取当前相机模式
    /// </summary>
    CameraMode GetCurrentMode() const { return m_CurrentMode; }

    /// <summary>
    /// 设置相机模式
    /// </summary>
    void SetCameraMode(CameraMode mode, entt::registry& registry);

    /// <summary>
    /// 切换相机模式（玩家/自由）
    /// </summary>
    void ToggleCameraMode(entt::registry& registry);
    
    /// <summary>
    /// 切换到飞船视角
    /// </summary>
    void SwitchToSpacecraftMode(entt::registry& registry, entt::entity spacecraft);
    
    /// <summary>
    /// 从飞船视角切换回玩家视角
    /// </summary>
    void ExitSpacecraftMode(entt::registry& registry);

private:
    /// <summary>
    /// 检测模式切换输入
    /// </summary>
    void CheckModeToggle(entt::registry& registry);

    /// <summary>
    /// 切换到玩家视角
    /// </summary>
    void SwitchToPlayerMode(entt::registry& registry);

    /// <summary>
    /// 切换到自由视角
    /// </summary>
    void SwitchToFreeMode(entt::registry& registry);

    /// <summary>
    /// 创建自由相机实体（如果不存在）
    /// </summary>
    void CreateFreeCameraEntity(entt::registry& registry);
    
    /// <summary>
    /// 更新飞船相机位置
    /// </summary>
    void UpdateSpacecraftCamera(entt::registry& registry);
    
    /// <summary>
    /// 检测玩家飞船驾驶状态，自动切换相机
    /// </summary>
    void CheckSpacecraftPilotingState(entt::registry& registry);

    std::shared_ptr<Scene> m_Scene;
    CameraMode m_CurrentMode = CameraMode::Player;
    entt::entity m_FreeCameraEntity = entt::null;  // 自由相机实体的引用
    entt::entity m_CurrentSpacecraft = entt::null; // 当前驾驶的飞船

    // 输入状态跟踪（防止重复切换）
    bool m_ShiftWasPressed = false;
    bool m_EscWasPressed = false;
};

} // namespace outer_wilds
