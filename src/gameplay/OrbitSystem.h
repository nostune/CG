#pragma once
#include "../core/ECS.h"
#include "components/OrbitComponent.h"
#include "../scene/components/TransformComponent.h"
#include <DirectXMath.h>

namespace outer_wilds {

/**
 * 轨道运动系统 - 更新所有带OrbitComponent的天体位置
 */
class OrbitSystem : public System {
public:
    void Update(float deltaTime, entt::registry& registry) override {
        auto view = registry.view<components::OrbitComponent, TransformComponent>();
        
        for (auto entity : view) {
            auto& orbit = view.get<components::OrbitComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);
            
            if (!orbit.isActive) continue;
            
            // 更新轨道角度
            orbit.currentAngle += orbit.GetAngularVelocity() * deltaTime;
            
            // 标准化角度到[0, 2π]
            while (orbit.currentAngle > DirectX::XM_2PI) {
                orbit.currentAngle -= DirectX::XM_2PI;
            }
            
            // 获取轨道中心位置
            DirectX::XMFLOAT3 centerPos = {0.0f, 0.0f, 0.0f};
            if (orbit.centerEntity != entt::null) {
                auto* centerTransform = registry.try_get<TransformComponent>(orbit.centerEntity);
                if (centerTransform) {
                    centerPos = centerTransform->position;
                }
            }
            
            // 计算轨道位置(圆形轨道,在XZ平面)
            float x = orbit.radius * cosf(orbit.currentAngle);
            float z = orbit.radius * sinf(orbit.currentAngle);
            
            // TODO: 支持任意轨道平面(通过orbitNormal旋转)
            // 当前简化版本:假设轨道在XZ平面
            
            transform.position.x = centerPos.x + x;
            transform.position.y = centerPos.y;
            transform.position.z = centerPos.z + z;
        }
    }
};

} // namespace outer_wilds
