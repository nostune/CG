#include "LightweightRenderQueue.h"
#include "components/RenderPriorityComponent.h"

namespace outer_wilds {

void LightweightRenderQueue::Sort(entt::registry& registry, bool frontToBack) {
    std::sort(m_Entities.begin(), m_Entities.end(),
        [&registry, frontToBack](entt::entity a, entt::entity b) {
            // 通过Entity ID访问组件数据进行比较
            auto* priorityA = registry.try_get<components::RenderPriorityComponent>(a);
            auto* priorityB = registry.try_get<components::RenderPriorityComponent>(b);
            
            // 如果没有Priority组件，放到最后
            if (!priorityA) return false;
            if (!priorityB) return true;
            
            // 第一级排序：按 renderPass 排序（确保 Opaque -> Transparent 顺序）
            if (priorityA->renderPass != priorityB->renderPass) {
                return priorityA->renderPass < priorityB->renderPass;
            }
            
            // 第二级排序：同一 renderPass 内的排序策略
            if (frontToBack) {
                // Opaque: 前到后排序（sortKey升序，利用Early-Z优化）
                return priorityA->sortKey < priorityB->sortKey;
            } else {
                // Transparent: 后到前排序（distanceToCamera降序，正确混合）
                return priorityA->distanceToCamera > priorityB->distanceToCamera;
            }
        });
}

} // namespace outer_wilds
