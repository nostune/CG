#pragma once
#include <DirectXMath.h>
#include "../../core/ECS.h"

namespace outer_wilds {
namespace components {

/**
 * 轨道运动组件 - 让天体沿椭圆轨道运动
 * 
 * 简化模型:使用圆形轨道,忽略偏心率和倾角
 */
struct OrbitComponent {
    // === 轨道参数 ===
    
    /** 轨道中心实体(例如地球是月球的轨道中心) */
    Entity centerEntity = entt::null;
    
    /** 轨道半径(米) */
    float radius = 10.0f;
    
    /** 轨道周期(秒) - 完成一圈的时间 */
    float period = 60.0f;
    
    /** 轨道平面法线(默认XZ平面,法线朝Y轴) */
    DirectX::XMFLOAT3 orbitNormal = {0.0f, 1.0f, 0.0f};
    
    // === 运行时状态 ===
    
    /** 当前轨道角度(弧度) */
    float currentAngle = 0.0f;
    
    /** 是否激活轨道运动 */
    bool isActive = true;
    
    // === 辅助方法 ===
    
    /** 获取角速度(弧度/秒) */
    float GetAngularVelocity() const {
        return (2.0f * DirectX::XM_PI) / period;
    }
};

} // namespace components
} // namespace outer_wilds
