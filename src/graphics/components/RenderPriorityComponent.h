#pragma once

#include <cstdint>

namespace outer_wilds {
namespace components {

/**
 * @brief 渲染优先级组件 - 用作"标签"管理渲染顺序
 * 
 * 这个组件不包含真实的渲染数据，只包含元数据用于：
 * - 排序（sortKey）
 * - 分类（renderPass）
 * - 剔除（visible）
 * - LOD（lodLevel）
 * 
 * 真实的渲染数据仍保留在 RenderableComponent 中
 */
struct RenderPriorityComponent {
    // 排序键：用于优化渲染顺序
    // 布局：Shader(8bit) | Material(8bit) | Depth(16bit)
    // 相同Shader和Material的物体会被连续渲染，减少状态切换
    uint32_t sortKey = 0;
    
    // 渲染通道：0=不透明, 1=透明, 2=UI, 3=阴影等
    uint8_t renderPass = 0;
    
    // LOD级别：0=高细节, 1=中, 2=低, 3=极低
    uint8_t lodLevel = 0;
    
    // 可见性标记（视锥剔除结果）
    bool visible = true;
    
    // 是否投射阴影
    bool castShadow = true;
    
    // 是否接收阴影
    bool receiveShadow = true;
    
    // 距离相机的距离（用于排序和LOD）
    float distanceToCamera = 0.0f;
    
    /**
     * @brief 计算排序键
     * @param shaderId 着色器ID（0-255）
     * @param materialId 材质ID（0-255）
     * @param depth 深度值（0-65535）
     */
    static uint32_t CalculateSortKey(uint8_t shaderId, uint8_t materialId, uint16_t depth) {
        return (static_cast<uint32_t>(shaderId) << 24) |
               (static_cast<uint32_t>(materialId) << 16) |
               static_cast<uint32_t>(depth);
    }
};

} // namespace components
} // namespace outer_wilds
