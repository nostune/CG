#pragma once
#include <DirectXMath.h>

using namespace DirectX;

namespace outer_wilds {
namespace components {

/**
 * 玩家对齐组件 - 控制玩家如何对齐到星球表面
 * 
 * 使玩家的"上方向"始终指向远离星球中心，实现在球面上行走的效果
 * 
 * 使用示例:
 *   PlayerAlignmentComponent alignment;
 *   alignment.alignmentSpeed = 5.0f;     // 平滑对齐速度
 *   alignment.autoAlign = true;          // 启用自动对齐
 *   registry.emplace<PlayerAlignmentComponent>(playerEntity, alignment);
 */
struct PlayerAlignmentComponent {
    // === 对齐行为 ===
    
    /** 对齐速度（单位：弧度/秒）
     *  较小值（2-3）：更平滑但反应慢
     *  中等值（5-8）：推荐，平衡感好
     *  较大值（10+）：立即对齐，可能抖动
     */
    float alignmentSpeed = 5.0f;
    
    /** 是否启用自动对齐到重力方向 */
    bool autoAlign = true;
    
    // === 约束 ===
    
    /** 锁定翻滚（防止玩家侧翻）
     *  true: 只允许前后倾斜和左右转向
     *  false: 完全自由旋转（可能导致相机翻转）
     */
    bool lockRoll = true;
    
    /** 保持前方向（在对齐时尽量保持玩家朝向）
     *  true: 对齐时只调整上方向，前方向尽量不变
     *  false: 完全重新计算旋转（可能突然转向）
     */
    bool preserveForward = true;
    
    // === 死区（避免微小抖动）===
    
    /** 对齐死区角度（度）
     *  如果当前上方向与目标上方向的夹角小于此值，不进行对齐
     */
    float alignmentDeadZone = 0.5f;
};

} // namespace components
} // namespace outer_wilds
