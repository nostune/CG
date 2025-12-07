#pragma once
#include "../core/ECS.h"
#include "../scene/Scene.h"
#include <entt/entt.hpp>
#include <memory>

namespace outer_wilds {

/// <summary>
/// 自由相机系统
/// 处理自由视角的移动和旋转，不受物理和重力影响
/// 使用WASD+鼠标控制，Space上升，Ctrl下降
/// </summary>
class FreeCameraSystem : public System {
public:
    void Initialize() override;
    void Initialize(std::shared_ptr<Scene> scene);
    void Update(float deltaTime, entt::registry& registry) override;
    void Shutdown() override;

private:
    /// <summary>
    /// 更新自由相机的移动
    /// </summary>
    void UpdateFreeCameraMovement(float deltaTime, entt::registry& registry);

    /// <summary>
    /// 更新自由相机的视角旋转
    /// </summary>
    void UpdateFreeCameraRotation(float deltaTime, entt::registry& registry);

    /// <summary>
    /// 同步自由相机位置到CameraComponent
    /// </summary>
    void SyncToCamera(entt::registry& registry);

    std::shared_ptr<Scene> m_Scene;
};

} // namespace outer_wilds
