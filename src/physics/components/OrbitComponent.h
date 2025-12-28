/**
 * OrbitComponent.h
 * 
 * 轨道组件 - 定义天体的公转轨道
 * 
 * 使用简化的圆形轨道模型：
 * - 围绕指定中心点公转
 * - 固定轨道半径和周期
 * - 可选自转
 */

#pragma once
#include <DirectXMath.h>
#include <entt/entt.hpp>

namespace outer_wilds {
namespace components {

/**
 * 轨道组件 - 附加到需要公转的天体（星球、卫星等）
 */
struct OrbitComponent {
    // ========================================
    // 公转参数
    // ========================================
    
    // 轨道中心（世界坐标）
    // 对于行星：围绕恒星
    // 对于卫星：围绕行星
    DirectX::XMFLOAT3 orbitCenter = { 0.0f, 0.0f, 0.0f };
    
    // 轨道中心实体（如果有的话，会自动跟随该实体）
    entt::entity orbitParent = entt::null;
    
    // 轨道半径（米）
    float orbitRadius = 200.0f;
    
    // 公转周期（秒）
    float orbitPeriod = 120.0f;
    
    // 当前轨道角度（弧度，0 = +X方向）
    float orbitAngle = 0.0f;
    
    // 轨道平面法线（默认Y轴，即水平面公转）
    DirectX::XMFLOAT3 orbitNormal = { 0.0f, 1.0f, 0.0f };
    
    // 轨道倾角（弧度，相对于 orbitNormal）
    float orbitInclination = 0.0f;
    
    // 是否启用公转
    bool orbitEnabled = true;
    
    // ========================================
    // 自转参数
    // ========================================
    
    // 自转周期（秒，0 = 不自转）
    float rotationPeriod = 0.0f;
    
    // 当前自转角度（弧度）
    float rotationAngle = 0.0f;
    
    // 自转轴（局部坐标系）
    DirectX::XMFLOAT3 rotationAxis = { 0.0f, 1.0f, 0.0f };
    
    // 是否启用自转
    bool rotationEnabled = false;
    
    // ========================================
    // 辅助函数
    // ========================================
    
    // 获取公转角速度（弧度/秒）
    float GetOrbitalAngularVelocity() const {
        if (orbitPeriod <= 0.0f) return 0.0f;
        return DirectX::XM_2PI / orbitPeriod;
    }
    
    // 获取自转角速度（弧度/秒）
    float GetRotationAngularVelocity() const {
        if (rotationPeriod <= 0.0f) return 0.0f;
        return DirectX::XM_2PI / rotationPeriod;
    }
    
    // 获取公转线速度（米/秒）
    float GetOrbitalLinearVelocity() const {
        return GetOrbitalAngularVelocity() * orbitRadius;
    }
};

} // namespace components
} // namespace outer_wilds
