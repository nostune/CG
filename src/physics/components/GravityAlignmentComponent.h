#pragma once
#include <DirectXMath.h>

namespace outer_wilds {
namespace components {

/**
 * 重力对齐组件 - 控制物体是否根据重力方向自动旋转
 * 
 * 设计理念：
 * - 简单物体（箱子、岩石）：使用简单碰撞器 + 强制对齐
 * - 复杂载具（飞船）：使用网格碰撞器 + 可选辅助对齐
 * - 降落辅助：玩家控制的平滑对齐过渡
 */
struct GravityAlignmentComponent {
    /** 
     * 对齐模式
     */
    enum class AlignmentMode {
        NONE,           // 不对齐（飞船自由飞行）
        FORCE_ALIGN,    // 强制对齐（箱子、岩石，防止滑动）
        ASSIST_ALIGN    // 辅助对齐（飞船降落辅助，玩家按键激活）
    };
    
    AlignmentMode mode = AlignmentMode::FORCE_ALIGN;
    
    /** 对齐速度（弧度/秒），0表示瞬间对齐 */
    float alignmentSpeed = 0.0f;  // 0 = instant, 2.0 = smooth
    
    /** 是否只在接地时对齐（飞行中不对齐） */
    bool alignOnlyWhenGrounded = false;
    
    /** 辅助对齐是否激活（由玩家输入控制） */
    bool assistAlignActive = false;
    
    /** 当前对齐进度（0-1，用于平滑过渡） */
    float alignmentProgress = 0.0f;
};

} // namespace components
} // namespace outer_wilds
