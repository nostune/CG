#pragma once
#include "../core/ECS.h"
#include "components/CelestialAttachmentComponent.h"
#include "../scene/components/TransformComponent.h"
#include <DirectXMath.h>

namespace outer_wilds {

/**
 * 天体附着系统 - 让物体跟随天体运动（保持相对位置）
 * 
 * 工作原理：
 * 1. 初始化时记录物体相对于天体的偏移
 * 2. 每帧更新物体的世界位置 = 天体位置 + 相对偏移
 * 3. 同步物体的旋转（可选）
 * 
 * 适用场景：
 * - 飞船停靠在行星表面
 * - 建筑物、地标等固定在行星上的物体
 */
class CelestialAttachmentSystem : public System {
public:
    void Update(float deltaTime, entt::registry& registry) override {
        using namespace DirectX;
        using namespace components;
        
        auto view = registry.view<CelestialAttachmentComponent, TransformComponent>();
        
        for (auto entity : view) {
            auto& attachment = view.get<CelestialAttachmentComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);
            
            if (!attachment.followEnabled || attachment.parentCelestial == entt::null) {
                continue;
            }
            
            // 获取父天体的Transform
            auto* parentTransform = registry.try_get<TransformComponent>(attachment.parentCelestial);
            if (!parentTransform) {
                continue;
            }
            
            // === 首次初始化：计算相对位置 ===
            if (!attachment.isInitialized) {
                XMVECTOR childPos = XMLoadFloat3(&transform.position);
                XMVECTOR parentPos = XMLoadFloat3(&parentTransform->position);
                XMVECTOR relativePos = XMVectorSubtract(childPos, parentPos);
                XMStoreFloat3(&attachment.relativePosition, relativePos);
                
                attachment.relativeRotation = transform.rotation;
                attachment.isInitialized = true;
                
                // Debug log
                static int initCount = 0;
                if (initCount++ < 3) {
                    std::cout << "[CelestialAttachmentSystem] Initialized entity at relative position ("
                              << attachment.relativePosition.x << ", "
                              << attachment.relativePosition.y << ", "
                              << attachment.relativePosition.z << ")" << std::endl;
                }
            }
            
            // === 每帧更新：应用相对位置 ===
            XMVECTOR parentPos = XMLoadFloat3(&parentTransform->position);
            XMVECTOR relativeOffset = XMLoadFloat3(&attachment.relativePosition);
            XMVECTOR newPosition = XMVectorAdd(parentPos, relativeOffset);
            XMStoreFloat3(&transform.position, newPosition);
            
            // 可选：同步旋转（如果需要物体朝向也跟随天体旋转）
            // transform.rotation = parentTransform->rotation;
        }
    }
};

} // namespace outer_wilds
