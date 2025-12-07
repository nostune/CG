#pragma once
#include <DirectXMath.h>
#include <entt/entt.hpp>

using namespace DirectX;

namespace outer_wilds {
namespace components {

/**
 * 受重力影响组件 - 附加到玩家、物体等需要受重力影响的实体上
 * 
 * 存储当前受到的重力状态，由GravitySystem每帧更新
 * 
 * 使用示例:
 *   GravityAffectedComponent gravity;
 *   gravity.gravityScale = 1.0f;         // 正常重力
 *   gravity.affectedByGravity = true;    // 启用重力
 *   registry.emplace<GravityAffectedComponent>(playerEntity, gravity);
 */
struct GravityAffectedComponent {
    // === 配置 ===
    
    /** 是否受重力影响 */
    bool affectedByGravity = true;
    
    /** 重力缩放因子
     *  0.0 = 无重力（漂浮）
     *  1.0 = 正常重力
     *  2.0 = 双倍重力
     */
    float gravityScale = 1.0f;
    
    // === 运行时状态（由GravitySystem更新）===
    
    /** 当前影响此实体的重力源（星球）*/
    entt::entity currentGravitySource = entt::null;
    
    /** 当前重力方向（归一化向量，指向星球中心）*/
    XMFLOAT3 currentGravityDir = { 0.0f, -1.0f, 0.0f };
    
    /** 当前重力强度（m/s²）*/
    float currentGravityStrength = 9.8f;
    
    // === 可选：平滑过渡 ===
    
    /** 上一帧的重力源（用于检测星球切换）*/
    entt::entity previousGravitySource = entt::null;
    
    /** 星球切换过渡进度（0-1）*/
    float transitionProgress = 1.0f;
    
    /** 星球切换过渡时长（秒）*/
    float transitionDuration = 2.0f;
    
    // === 辅助方法 ===
    
    /** 检查是否正在切换星球 */
    bool IsSwitchingGravitySource() const {
        return transitionProgress < 1.0f;
    }
    
    /** 获取当前有效的重力加速度向量 */
    XMFLOAT3 GetGravityAcceleration() const {
        if (!affectedByGravity) {
            return { 0.0f, 0.0f, 0.0f };
        }
        
        float strength = currentGravityStrength * gravityScale;
        return {
            currentGravityDir.x * strength,
            currentGravityDir.y * strength,
            currentGravityDir.z * strength
        };
    }
    
    /** 获取"上方向"（与重力相反，用于对齐玩家）*/
    XMFLOAT3 GetUpDirection() const {
        return {
            -currentGravityDir.x,
            -currentGravityDir.y,
            -currentGravityDir.z
        };
    }
};

} // namespace components
} // namespace outer_wilds
