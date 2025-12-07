#pragma once
#include "../core/ECS.h"
#include "components/PlayerComponent.h"
#include "components/PlayerAlignmentComponent.h"
#include "../physics/components/GravityAffectedComponent.h"
#include "../scene/components/TransformComponent.h"
#include <DirectXMath.h>
#include <entt/entt.hpp>

using namespace DirectX;
using namespace outer_wilds;
using namespace outer_wilds::components;

namespace outer_wilds {

/**
 * 玩家对齐系统 - 使玩家姿态对齐到星球表面
 * 
 * 职责:
 *  1. 读取 GravityAffectedComponent 的重力方向
 *  2. 计算玩家的目标旋转（脚朝地心，头朝外太空）
 *  3. 平滑插值玩家的旋转到目标方向
 * 
 * 更新顺序: 在GravitySystem之后，PlayerSystem（移动）之前
 */
class PlayerAlignmentSystem : public System {
public:
    void Update(float deltaTime, entt::registry& registry) override {
        // 遍历所有玩家实体
        auto playerView = registry.view<
            PlayerComponent,
            PlayerAlignmentComponent,
            GravityAffectedComponent,
            TransformComponent
        >();
        
        for (auto entity : playerView) {
            auto& alignment = playerView.get<PlayerAlignmentComponent>(entity);
            auto& gravity = playerView.get<GravityAffectedComponent>(entity);
            auto& transform = playerView.get<TransformComponent>(entity);
            
            if (!alignment.autoAlign) {
                continue;  // 跳过禁用自动对齐的玩家
            }
            
            // 1. 获取目标上方向（与重力相反）
            XMVECTOR desiredUp = XMLoadFloat3(&gravity.GetUpDirection());
            
            // 2. 获取当前上方向
            XMVECTOR currentRotation = XMLoadFloat4(&transform.rotation);
            XMVECTOR currentUp = XMVector3Rotate(XMVectorSet(0, 1, 0, 0), currentRotation);
            
            // 3. 检查死区（避免微小抖动）
            float angle = XMVectorGetX(XMVector3AngleBetweenNormals(currentUp, desiredUp));
            float deadZoneRad = XMConvertToRadians(alignment.alignmentDeadZone);
            
            if (angle < deadZoneRad) {
                continue;  // 已经足够对齐，跳过
            }
            
            // 4. 计算目标旋转
            XMVECTOR targetRotation;
            
            if (alignment.preserveForward) {
                // 保持前方向，只调整上方向
                targetRotation = CalculateRotationPreservingForward(
                    currentRotation,
                    desiredUp,
                    alignment.lockRoll
                );
            }
            else {
                // 完全重新计算旋转
                XMVECTOR currentForward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), currentRotation);
                targetRotation = CreateLookRotation(currentForward, desiredUp);
            }
            
            // 5. 平滑插值到目标旋转
            float t = (alignment.alignmentSpeed * deltaTime < 1.0f) ? (alignment.alignmentSpeed * deltaTime) : 1.0f;
            XMVECTOR newRotation = XMQuaternionSlerp(currentRotation, targetRotation, t);
            
            XMStoreFloat4(&transform.rotation, newRotation);
        }
    }

private:
    /**
     * 计算旋转，保持当前前方向，只调整上方向
     * 
     * 这种方式避免玩家在对齐时突然转向，体验更好
     */
    XMVECTOR CalculateRotationPreservingForward(
        XMVECTOR currentRotation,
        XMVECTOR desiredUp,
        bool lockRoll
    ) const {
        // 1. 获取当前前方向
        XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), currentRotation);
        
        // 2. 计算新的右方向（forward × desiredUp）
        XMVECTOR right = XMVector3Normalize(XMVector3Cross(desiredUp, forward));
        
        // 3. 重新计算前方向（desiredUp × right），确保正交
        forward = XMVector3Cross(right, desiredUp);
        
        // 4. 从三个轴构造旋转矩阵
        XMMATRIX rotationMatrix = ConstructRotationMatrix(forward, desiredUp, right);
        
        // 5. 转换为四元数
        XMVECTOR rotation = XMQuaternionRotationMatrix(rotationMatrix);
        
        // 6. 如果锁定翻滚，移除roll分量
        if (lockRoll) {
            rotation = RemoveRoll(rotation, desiredUp);
        }
        
        return rotation;
    }
    
    /**
     * 创建LookRotation（类似Unity的Quaternion.LookRotation）
     * 
     * @param forward 前方向
     * @param up 上方向
     * @return 对应的四元数旋转
     */
    XMVECTOR CreateLookRotation(XMVECTOR forward, XMVECTOR up) const {
        // 归一化输入
        forward = XMVector3Normalize(forward);
        up = XMVector3Normalize(up);
        
        // 计算右方向
        XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, forward));
        
        // 重新计算上方向（确保正交）
        up = XMVector3Cross(forward, right);
        
        // 构造旋转矩阵
        XMMATRIX rotationMatrix = ConstructRotationMatrix(forward, up, right);
        
        return XMQuaternionRotationMatrix(rotationMatrix);
    }
    
    /**
     * 从三个轴向量构造旋转矩阵
     */
    XMMATRIX ConstructRotationMatrix(XMVECTOR forward, XMVECTOR up, XMVECTOR right) const {
        XMMATRIX mat;
        
        // DirectX是行主序，列向量表示轴
        // Right轴 (X)
        mat.r[0] = XMVectorSetW(right, 0.0f);
        
        // Up轴 (Y)
        mat.r[1] = XMVectorSetW(up, 0.0f);
        
        // Forward轴 (Z)
        mat.r[2] = XMVectorSetW(forward, 0.0f);
        
        // 齐次坐标
        mat.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
        
        return mat;
    }
    
    /**
     * 移除四元数的roll（翻滚）分量
     * 
     * 保持yaw和pitch，但消除绕forward轴的旋转
     */
    XMVECTOR RemoveRoll(XMVECTOR rotation, XMVECTOR desiredUp) const {
        // 提取欧拉角
        XMFLOAT4 quat;
        XMStoreFloat4(&quat, rotation);
        
        // 简化版：重新投影到只允许yaw和pitch的旋转
        // （完整实现需要更复杂的欧拉角分解）
        
        // 获取当前前方向和右方向
        XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), rotation);
        XMVECTOR right = XMVector3Normalize(XMVector3Cross(desiredUp, forward));
        forward = XMVector3Cross(right, desiredUp);
        
        // 重新构造旋转
        XMMATRIX mat = ConstructRotationMatrix(forward, desiredUp, right);
        return XMQuaternionRotationMatrix(mat);
    }
};

} // namespace outer_wilds
