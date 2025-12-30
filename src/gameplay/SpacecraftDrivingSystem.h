/**
 * SpacecraftDrivingSystem.h
 * 
 * 飞船驾驶系统 - 基于物理的6自由度飞船控制
 * 
 * 控制方案：
 * - W/S: 前后推进（主引擎）
 * - A/D: 左右平移（侧向推进器）
 * - Shift/Ctrl: 上下推进（垂直推进器）
 * - Q/E: 滚转（绕前向轴旋转）
 * - 方向键上下: 俯仰（绕右向轴旋转）
 * - 方向键左右: 偏航（绕上向轴旋转）
 * 
 * 所有运动通过 PhysX 力/力矩实现，基于飞船局部坐标系
 */

#pragma once
#include "../core/ECS.h"
#include "../scene/Scene.h"
#include <DirectXMath.h>
#include <memory>

namespace outer_wilds {

class SpacecraftDrivingSystem : public System {
public:
    SpacecraftDrivingSystem() = default;
    ~SpacecraftDrivingSystem() override = default;

    void Initialize() override;
    void Initialize(std::shared_ptr<Scene> scene);
    void Update(float deltaTime, entt::registry& registry) override;
    void Shutdown() override;

private:
    /**
     * 处理飞船输入
     * 读取键盘状态，更新 SpacecraftComponent 的输入字段
     */
    void ProcessSpacecraftInput(entt::registry& registry);
    
    /**
     * 应用飞船力和力矩
     * 根据输入状态，向 PhysX 刚体施加力和力矩
     */
    void ApplySpacecraftForces(float deltaTime, entt::registry& registry);
    
    /**
     * 获取飞船局部坐标系方向向量
     * 从飞船的旋转四元数提取 forward, right, up 方向
     */
    void GetSpacecraftAxes(
        const DirectX::XMFLOAT4& rotation,
        DirectX::XMFLOAT3& forward,
        DirectX::XMFLOAT3& right,
        DirectX::XMFLOAT3& up
    );
    
    /**
     * 更新飞船状态信息
     * 从 PhysX 读取速度信息，更新调试显示
     */
    void UpdateSpacecraftState(entt::registry& registry);
    
    /**
     * 更新飞船第三人称跟随相机
     * 相机在飞船后上方，平滑跟随飞船移动和旋转
     */
    void UpdateSpacecraftCamera(float deltaTime, entt::registry& registry);

    std::shared_ptr<Scene> m_Scene;
    
    // 输入平滑参数
    float m_InputSmoothSpeed = 5.0f;  // 输入过渡速度
    
    // 相机平滑跟随状态
    DirectX::XMFLOAT3 m_CurrentCameraPos = { 0.0f, 0.0f, 0.0f };
    bool m_CameraInitialized = false;
};

} // namespace outer_wilds
