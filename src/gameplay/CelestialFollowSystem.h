#pragma once
#include "../core/ECS.h"
#include "components/StandingOnComponent.h"
#include "components/CharacterControllerComponent.h"
#include "../scene/components/TransformComponent.h"
#include "../physics/components/GravityAffectedComponent.h"
#include "../physics/components/GravitySourceComponent.h"
#include <DirectXMath.h>
#include <PxPhysicsAPI.h>

namespace outer_wilds {

/**
 * 天体跟随系统 - 让玩家跟随移动的天体（如轨道运动的月球）
 * 
 * 工作原理:
 * 1. 记录上一帧天体的位置
 * 2. 检测玩家当前站在哪个天体上（基于重力源）
 * 3. 计算天体本帧的位移量
 * 4. 将这个位移量应用到玩家的CharacterController上
 * 
 * 更新顺序: 必须在PlayerSystem之后运行（PlayerSystem会覆盖Transform）
 */
class CelestialFollowSystem : public System {
public:
    void Update(float deltaTime, entt::registry& registry) override {
        using namespace DirectX;
        using namespace components;
        
        auto view = registry.view<StandingOnComponent, GravityAffectedComponent, CharacterControllerComponent>();
        
        for (auto entity : view) {
            auto& standing = view.get<StandingOnComponent>(entity);
            auto& gravity = view.get<GravityAffectedComponent>(entity);
            auto& character = view.get<CharacterControllerComponent>(entity);
            
            if (!standing.followEnabled || !character.controller) continue;
            
            // === 步骤1: 确定当前站在哪个天体上 ===
            Entity currentCelestial = DetermineStandingEntity(registry, gravity);
            
            // === 步骤2: 如果站在天体上，应用天体位移 ===
            if (currentCelestial != entt::null) {
                auto* celestialTransform = registry.try_get<TransformComponent>(currentCelestial);
                if (celestialTransform) {
                    // 检测天体切换
                    if (currentCelestial != standing.standingOnEntity) {
                        standing.previousStandingEntity = standing.standingOnEntity;
                        standing.standingOnEntity = currentCelestial;
                        // 记录新天体的位置作为"上一帧位置"
                        standing.relativePosition = celestialTransform->position;
                    }
                    
                    // 计算天体本帧的位移
                    XMVECTOR lastPos = XMLoadFloat3(&standing.relativePosition);
                    XMVECTOR currentPos = XMLoadFloat3(&celestialTransform->position);
                    XMVECTOR displacement = XMVectorSubtract(currentPos, lastPos);
                    
                    // 只有天体实际移动了才应用位移
                    float displacementMagnitude = XMVectorGetX(XMVector3Length(displacement));
                    if (displacementMagnitude > 0.0001f) {
                        // 应用位移到玩家
                        ApplyCelestialDisplacement(character, displacement);
                        
                        // 更新记录的天体位置
                        standing.relativePosition = celestialTransform->position;
                    }
                }
            } else {
                // 不在任何天体上，清空记录
                standing.standingOnEntity = entt::null;
            }
        }
    }
    
private:
    /**
     * 确定实体站在哪个天体上（简化版：直接使用重力源）
     */
    Entity DetermineStandingEntity(
        entt::registry& registry,
        const GravityAffectedComponent& gravity
    ) {
        // 直接使用当前的重力源
        return gravity.currentGravitySource;
    }
    
    /**
     * 应用天体位移到玩家CharacterController
     */
    void ApplyCelestialDisplacement(
        components::CharacterControllerComponent& character,
        DirectX::XMVECTOR displacement
    ) {
        using namespace DirectX;
        
        // 获取当前CharacterController位置
        physx::PxExtendedVec3 currentPos = character.controller->getFootPosition();
        
        // 应用位移
        XMVECTOR playerPos = XMVectorSet((float)currentPos.x, (float)currentPos.y, (float)currentPos.z, 0.0f);
        XMVECTOR newPos = XMVectorAdd(playerPos, displacement);
        
        // 设置新位置（传送玩家）
        XMFLOAT3 newPosF;
        XMStoreFloat3(&newPosF, newPos);
        physx::PxExtendedVec3 pxNewPos(newPosF.x, newPosF.y, newPosF.z);
        character.controller->setFootPosition(pxNewPos);
    }
};

} // namespace outer_wilds
