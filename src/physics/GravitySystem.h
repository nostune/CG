#pragma once
#include "../core/ECS.h"
#include "components/GravitySourceComponent.h"
#include "components/GravityAffectedComponent.h"
#include "../scene/components/TransformComponent.h"
#include <DirectXMath.h>
#include <entt/entt.hpp>
#include <limits>

using namespace DirectX;
using namespace outer_wilds;
using namespace outer_wilds::components;

namespace outer_wilds {

/**
 * 重力系统 - 计算并应用星球重力
 * 
 * 职责:
 *  1. 为每个受影响的实体找到最近的重力源（星球）
 *  2. 计算重力方向（指向星球中心）和强度
 *  3. 更新 GravityAffectedComponent 的状态
 *  4. 处理星球切换的平滑过渡
 * 
 * 更新顺序: 在PhysicsSystem之前调用，这样PhysX可以使用更新后的重力信息
 */
class GravitySystem : public System {
public:
    void Update(float deltaTime, entt::registry& registry) override {
        // 遍历所有受重力影响的实体
        auto affectedView = registry.view<GravityAffectedComponent, TransformComponent>();
        
        static int debugFrame = 0;
        bool shouldDebug = (++debugFrame % 120 == 0); // 每2秒输出一次
        
        for (auto entity : affectedView) {
            auto& affected = affectedView.get<GravityAffectedComponent>(entity);
            auto& transform = affectedView.get<TransformComponent>(entity);
            
            if (!affected.affectedByGravity) {
                continue;  // 跳过禁用重力的实体
            }
            
            // 1. 找到最近的重力源
            entt::entity nearestSource = FindNearestGravitySource(registry, transform.position);
            
            if (shouldDebug) {
                char buffer[256];
                sprintf_s(buffer, "[GravitySystem] Entity pos=(%.1f,%.1f,%.1f), %s",
                    transform.position.x,
                    transform.position.y,
                    transform.position.z,
                    (nearestSource != entt::null) ? "Found gravity source" : "NO gravity source found!");
                outer_wilds::DebugManager::GetInstance().Log("Gravity", buffer);
            }
            
            if (nearestSource != entt::null) {
                // 2. 检测星球切换
                if (nearestSource != affected.currentGravitySource && 
                    affected.currentGravitySource != entt::null) {
                    // 开始星球切换过渡
                    affected.previousGravitySource = affected.currentGravitySource;
                    affected.transitionProgress = 0.0f;
                }
                
                affected.currentGravitySource = nearestSource;
                
                // 3. 计算重力方向和强度
                CalculateGravity(registry, nearestSource, affected, transform);
                
                if (shouldDebug) {
                    char buffer[256];
                    sprintf_s(buffer, "  GravityDir=(%.2f,%.2f,%.2f), Strength=%.2f",
                        affected.currentGravityDir.x,
                        affected.currentGravityDir.y,
                        affected.currentGravityDir.z,
                        affected.currentGravityStrength);
                    outer_wilds::DebugManager::GetInstance().Log("Gravity", buffer);
                }
                
                // 4. 更新过渡状态
                if (affected.IsSwitchingGravitySource()) {
                    affected.transitionProgress += deltaTime / affected.transitionDuration;
                    if (affected.transitionProgress >= 1.0f) {
                        affected.transitionProgress = 1.0f;
                        affected.previousGravitySource = entt::null;
                    }
                }
            }
            else {
                // 没有重力源 - 进入失重状态
                affected.currentGravitySource = entt::null;
                affected.currentGravityStrength = 0.0f;
            }
        }
    }

private:
    /**
     * 查找离指定位置最近的活跃重力源
     * 
     * @param registry ECS注册表
     * @param position 查询位置
     * @return 最近的重力源实体，如果没有在影响范围内则返回 entt::null
     */
    entt::entity FindNearestGravitySource(entt::registry& registry, const XMFLOAT3& position) {
        entt::entity nearest = entt::null;
        float minDistance = (std::numeric_limits<float>::max)();
        
        // 遍历所有重力源
        auto sourceView = registry.view<GravitySourceComponent, TransformComponent>();
        
        for (auto entity : sourceView) {
            auto& source = sourceView.get<GravitySourceComponent>(entity);
            auto& transform = sourceView.get<TransformComponent>(entity);
            
            if (!source.isActive) {
                continue;  // 跳过未激活的重力源
            }
            
            // 计算距离
            XMVECTOR posVec = XMLoadFloat3(&position);
            XMVECTOR sourceVec = XMLoadFloat3(&transform.position);
            XMVECTOR diff = XMVectorSubtract(posVec, sourceVec);
            float distance = XMVectorGetX(XMVector3Length(diff));
            
            // 检查是否在影响范围内
            float influenceRadius = source.GetInfluenceRadius();
            
            if (distance < influenceRadius && distance < minDistance) {
                nearest = entity;
                minDistance = distance;
            }
        }
        
        return nearest;
    }
    
    /**
     * 计算指定重力源对实体的重力影响
     * 
     * @param registry ECS注册表
     * @param sourceEntity 重力源实体
     * @param affected 受影响组件（输出）
     * @param transform 实体的变换组件
     */
    void CalculateGravity(
        entt::registry& registry,
        entt::entity sourceEntity,
        GravityAffectedComponent& affected,
        const TransformComponent& transform
    ) {
        // 获取重力源组件
        auto& source = registry.get<GravitySourceComponent>(sourceEntity);
        auto& sourceTransform = registry.get<TransformComponent>(sourceEntity);
        
        // 1. 计算重力方向（从实体指向星球中心）
        XMVECTOR entityPos = XMLoadFloat3(&transform.position);
        XMVECTOR sourcePos = XMLoadFloat3(&sourceTransform.position);
        XMVECTOR gravityDirVec = XMVectorSubtract(sourcePos, entityPos);
        
        // 计算距离
        float distance = XMVectorGetX(XMVector3Length(gravityDirVec));
        
        // 归一化重力方向
        if (distance > 0.001f) {  // 避免除以0
            gravityDirVec = XMVector3Normalize(gravityDirVec);
        }
        else {
            // 距离过近，使用默认向下
            gravityDirVec = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
        }
        
        XMStoreFloat3(&affected.currentGravityDir, gravityDirVec);
        
        // 2. 计算重力强度
        affected.currentGravityStrength = source.CalculateGravityStrength(distance);
    }
};

} // namespace outer_wilds
