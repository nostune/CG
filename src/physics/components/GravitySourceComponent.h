#pragma once
#include <DirectXMath.h>

namespace outer_wilds {
namespace components {

/**
 * 重力源组件 - 附加到星球等天体上
 * 
 * 定义天体的重力场属性，影响范围内带有GravityAffectedComponent的实体
 * 
 * 使用示例:
 *   GravitySourceComponent gravity;
 *   gravity.radius = 20.0f;              // 星球半径
 *   gravity.surfaceGravity = 9.8f;       // 表面重力加速度
 *   gravity.atmosphereHeight = 5.0f;     // 影响范围扩展
 *   registry.emplace<GravitySourceComponent>(planetEntity, gravity);
 */
struct GravitySourceComponent {
    // === 物理属性 ===
    
    /** 星球质量（kg） - 用于真实重力计算 */
    float mass = 1000.0f;
    
    /** 星球半径（米） - 用于距离计算和影响范围 */
    float radius = 10.0f;
    
    /** 表面重力加速度（m/s²） - 简化模式下使用 */
    float surfaceGravity = 9.8f;
    
    /** 大气层高度（米） - 扩展影响范围，用于平滑过渡 */
    float atmosphereHeight = 5.0f;
    
    // === 行为选项 ===
    
    /** 是否使用真实物理重力衰减（F = G*M*m / r²）
     *  false: 在影响范围内使用恒定的 surfaceGravity（推荐，游戏性好）
     *  true:  使用真实物理公式（适合模拟真实太空）
     */
    bool useRealisticGravity = false;
    
    /** 重力源是否激活 */
    bool isActive = true;
    
    // === 辅助方法 ===
    
    /** 获取总影响半径（星球半径 + 大气层） */
    float GetInfluenceRadius() const {
        return radius + atmosphereHeight;
    }
    
    /** 计算指定距离处的重力强度 */
    float CalculateGravityStrength(float distance) const {
        if (!isActive) return 0.0f;
        
        if (useRealisticGravity) {
            // 真实重力公式（简化版，不使用实际G常数）
            // 在半径处应该等于 surfaceGravity
            float normalizedDistance = distance / radius;
            return surfaceGravity / (normalizedDistance * normalizedDistance);
        }
        else {
            // 简化模式：表面附近恒定，远处衰减
            if (distance < radius * 1.5f) {
                // 靠近表面 - 恒定重力
                return surfaceGravity;
            }
            else {
                // 远离表面 - 线性衰减到大气层边界
                float maxDist = GetInfluenceRadius();
                float t = (distance - radius * 1.5f) / (maxDist - radius * 1.5f);
                return surfaceGravity * (1.0f - t);
            }
        }
    }
};

} // namespace components
} // namespace outer_wilds
