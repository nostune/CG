#pragma once

#include "../core/ECS.h"
#include <vector>
#include <algorithm>

namespace outer_wilds {

// 前向声明
namespace components {
    struct RenderPriorityComponent;
}

/**
 * @brief 轻量级渲染队列 - 只存储Entity ID
 * 
 * 设计原则：
 * 1. 只存储Entity ID（4-8 bytes per entity）
 * 2. 不拷贝真实的渲染数据（Mesh/Material/Transform）
 * 3. 排序时通过ID访问组件进行比较
 * 4. 渲染时通过ID访问组件数据
 * 
 * 优势：
 * - 内存占用小（1000个物体仅4-8KB）
 * - 保持ECS纯粹性（数据仍在Components中）
 * - 支持灵活的排序和过滤
 */
class LightweightRenderQueue {
public:
    LightweightRenderQueue() = default;
    ~LightweightRenderQueue() = default;

    /**
     * @brief 清空队列
     */
    void Clear() {
        m_Entities.clear();
    }

    /**
     * @brief 添加实体ID到队列
     * @param entity 实体ID
     */
    void Push(entt::entity entity) {
        m_Entities.push_back(entity);
    }

    /**
     * @brief 预留空间
     * @param capacity 容量
     */
    void Reserve(size_t capacity) {
        m_Entities.reserve(capacity);
    }

    /**
     * @brief 按RenderPriorityComponent的sortKey排序
     * @param registry ECS注册表
     * @param frontToBack true=前向后排序（不透明物体），false=后向前排序（透明物体）
     */
    void Sort(entt::registry& registry, bool frontToBack = true);

    /**
     * @brief 自定义排序
     * @param comparator 比较函数
     */
    template<typename Comparator>
    void SortCustom(Comparator comparator) {
        std::sort(m_Entities.begin(), m_Entities.end(), comparator);
    }

    /**
     * @brief 获取实体ID列表（只读）
     */
    const std::vector<entt::entity>& GetEntities() const {
        return m_Entities;
    }

    /**
     * @brief 获取队列大小
     */
    size_t Size() const {
        return m_Entities.size();
    }

    /**
     * @brief 队列是否为空
     */
    bool Empty() const {
        return m_Entities.empty();
    }

private:
    // 只存储Entity ID，不存储真实数据
    std::vector<entt::entity> m_Entities;
};

} // namespace outer_wilds
