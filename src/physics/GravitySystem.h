#pragma once
#include "../core/ECS.h"
#include "components/GravitySourceComponent.h"
#include "components/GravityAffectedComponent.h"
#include "components/SectorComponent.h"
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
 * ============================================================
 * Sector 系统集成
 * ============================================================
 * 
 * 当实体在 Sector 中时：
 * - 星球中心在局部坐标系中是 (0,0,0)
 * - 重力方向 = -normalize(实体局部位置)
 * - 不需要查找重力源，直接使用 Sector 的重力源
 * 
 * 职责:
 *  1. 检查实体是否在 Sector 中
 *  2. 如果在 Sector 中，使用局部坐标计算重力
 *  3. 如果不在 Sector 中，使用世界坐标查找最近重力源
 */
class GravitySystem : public System {
public:
    void Update(float deltaTime, entt::registry& registry) override {
        // 遍历所有受重力影响的实体
        auto affectedView = registry.view<GravityAffectedComponent, TransformComponent>();
        
        for (auto entity : affectedView) {
            auto& affected = affectedView.get<GravityAffectedComponent>(entity);
            auto& transform = affectedView.get<TransformComponent>(entity);
            
            if (!affected.affectedByGravity) {
                continue;  // 跳过禁用重力的实体
            }
            
            // === 关键修改：检查是否在 Sector 中 ===
            auto* inSector = registry.try_get<InSectorComponent>(entity);
            if (inSector && inSector->currentSector != entt::null && inSector->isInitialized) {
                // 在 Sector 中：使用局部坐标计算重力
                CalculateGravityInSector(registry, entity, *inSector, affected);
            } else {
                // 不在 Sector 中：使用世界坐标查找重力源
                CalculateGravityFromWorld(registry, entity, affected, transform, deltaTime);
            }
        }
    }

private:
    /**
     * 在 Sector 中计算重力
     * 星球中心 = (0,0,0)，重力方向 = -normalize(localPosition)
     */
    void CalculateGravityInSector(
        entt::registry& registry,
        entt::entity entity,
        const InSectorComponent& inSector,
        GravityAffectedComponent& affected
    ) {
        // 获取 Sector 的重力源组件
        auto* sectorGravity = registry.try_get<GravitySourceComponent>(inSector.currentSector);
        if (!sectorGravity || !sectorGravity->isActive) {
            affected.currentGravityStrength = 0.0f;
            return;
        }
        
        // 局部坐标系中，星球中心是 (0,0,0)
        // 重力方向 = 从实体指向中心 = -normalize(localPosition)
        XMVECTOR localPos = XMLoadFloat3(&inSector.localPosition);
        float distance = XMVectorGetX(XMVector3Length(localPos));
        
        if (distance > 0.001f) {
            XMVECTOR gravityDir = XMVector3Normalize(XMVectorNegate(localPos));
            XMStoreFloat3(&affected.currentGravityDir, gravityDir);
        } else {
            // 在中心点，使用默认向下
            affected.currentGravityDir = {0.0f, -1.0f, 0.0f};
        }
        
        // 计算重力强度
        affected.currentGravityStrength = sectorGravity->CalculateGravityStrength(distance);
        affected.currentGravitySource = inSector.currentSector;
    }
    
    /**
     * 使用世界坐标计算重力（用于不在 Sector 中的实体）
     */
    void CalculateGravityFromWorld(
        entt::registry& registry,
        entt::entity entity,
        GravityAffectedComponent& affected,
        const TransformComponent& transform,
        float deltaTime
    ) {
        // 找到最近的重力源
        entt::entity nearestSource = FindNearestGravitySource(registry, transform.position);
        
        if (nearestSource != entt::null) {
            // 检测星球切换
            if (nearestSource != affected.currentGravitySource && 
                affected.currentGravitySource != entt::null) {
                affected.previousGravitySource = affected.currentGravitySource;
                affected.transitionProgress = 0.0f;
            }
            
            affected.currentGravitySource = nearestSource;
            CalculateGravity(registry, nearestSource, affected, transform);
            
            // 更新过渡状态
            if (affected.IsSwitchingGravitySource()) {
                affected.transitionProgress += deltaTime / affected.transitionDuration;
                if (affected.transitionProgress >= 1.0f) {
                    affected.transitionProgress = 1.0f;
                    affected.previousGravitySource = entt::null;
                }
            }
        } else {
            // 没有重力源 - 失重状态
            affected.currentGravitySource = entt::null;
            affected.currentGravityStrength = 0.0f;
        }
    }
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
            
            // === 关键修复：限制查找范围，避免月球干扰 ===
            // 只在星球表面附近才附着（半径 + 小边界）
            float attachmentRange = source.radius + 10.0f;  // 只在表面上方10米范围内
            
            // 检查是否在附着范围内（而非影响范围）
            if (distance < attachmentRange && distance < minDistance) {
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
