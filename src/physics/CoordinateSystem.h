#pragma once
#include <DirectXMath.h>
#include <PxPhysicsAPI.h>

namespace outer_wilds {

/**
 * CoordinateSystem - 坐标系转换工具
 * 
 * 处理 PhysX（右手系）和 DirectX（左手系）之间的坐标转换。
 * 
 * 坐标系定义：
 * - PhysX (右手系): X右, Y上, Z朝向观察者
 * - DirectX (左手系): X右, Y上, Z远离观察者
 * 
 * 转换规则：
 * - 位置: Z 轴翻转 (z_dx = -z_px)
 * - 旋转: Z 和 W 分量符号变化（四元数转换）
 * 
 * 使用约定：
 * - "World" 坐标 = DirectX 左手系（用于渲染）
 * - "Physics" 坐标 = PhysX 右手系（用于物理模拟）
 * - "Sector Local" 坐标 = 使用 Physics 坐标系（因为物理在这里运行）
 */
class CoordinateSystem {
public:
    // ============================================
    // 位置转换
    // ============================================
    
    /**
     * PhysX 位置 → DirectX 位置
     * 翻转 Z 轴
     */
    static DirectX::XMFLOAT3 PhysicsToWorld(const physx::PxVec3& physicsPos) {
        return DirectX::XMFLOAT3(
            physicsPos.x,
            physicsPos.y,
            -physicsPos.z  // Z 轴翻转
        );
    }
    
    static DirectX::XMFLOAT3 PhysicsToWorld(const DirectX::XMFLOAT3& physicsPos) {
        return DirectX::XMFLOAT3(
            physicsPos.x,
            physicsPos.y,
            -physicsPos.z
        );
    }
    
    /**
     * DirectX 位置 → PhysX 位置
     * 翻转 Z 轴
     */
    static physx::PxVec3 WorldToPhysics(const DirectX::XMFLOAT3& worldPos) {
        return physx::PxVec3(
            worldPos.x,
            worldPos.y,
            -worldPos.z  // Z 轴翻转
        );
    }
    
    static DirectX::XMFLOAT3 WorldToPhysicsFloat3(const DirectX::XMFLOAT3& worldPos) {
        return DirectX::XMFLOAT3(
            worldPos.x,
            worldPos.y,
            -worldPos.z
        );
    }
    
    // ============================================
    // 旋转转换（四元数）
    // ============================================
    
    /**
     * PhysX 四元数 → DirectX 四元数
     * 
     * 从右手系到左手系的四元数转换：
     * 需要翻转旋转轴的 Z 分量和旋转方向
     * q_dx = (qx, qy, -qz, qw) 或 (-qx, -qy, qz, -qw)
     * 
     * 常用方法：翻转 Z 和 W
     */
    static DirectX::XMFLOAT4 PhysicsToWorld(const physx::PxQuat& physicsRot) {
        return DirectX::XMFLOAT4(
            physicsRot.x,
            physicsRot.y,
            -physicsRot.z,  // Z 轴翻转
            physicsRot.w    // W 保持（表示 cos(θ/2)）
        );
    }
    
    static DirectX::XMFLOAT4 PhysicsToWorld(const DirectX::XMFLOAT4& physicsRot) {
        return DirectX::XMFLOAT4(
            physicsRot.x,
            physicsRot.y,
            -physicsRot.z,
            physicsRot.w
        );
    }
    
    /**
     * DirectX 四元数 → PhysX 四元数
     */
    static physx::PxQuat WorldToPhysics(const DirectX::XMFLOAT4& worldRot) {
        return physx::PxQuat(
            worldRot.x,
            worldRot.y,
            -worldRot.z,  // Z 轴翻转
            worldRot.w
        );
    }
    
    static DirectX::XMFLOAT4 WorldToPhysicsFloat4(const DirectX::XMFLOAT4& worldRot) {
        return DirectX::XMFLOAT4(
            worldRot.x,
            worldRot.y,
            -worldRot.z,
            worldRot.w
        );
    }
    
    // ============================================
    // 速度/向量转换
    // ============================================
    
    /**
     * PhysX 向量 → DirectX 向量
     */
    static DirectX::XMFLOAT3 PhysicsVectorToWorld(const physx::PxVec3& physicsVec) {
        return DirectX::XMFLOAT3(
            physicsVec.x,
            physicsVec.y,
            -physicsVec.z
        );
    }
    
    /**
     * DirectX 向量 → PhysX 向量
     */
    static physx::PxVec3 WorldVectorToPhysics(const DirectX::XMFLOAT3& worldVec) {
        return physx::PxVec3(
            worldVec.x,
            worldVec.y,
            -worldVec.z
        );
    }
    
    // ============================================
    // Transform 转换
    // ============================================
    
    /**
     * PhysX Transform → DirectX 位置和旋转
     */
    static void PhysicsToWorld(
        const physx::PxTransform& physicsTransform,
        DirectX::XMFLOAT3& outPosition,
        DirectX::XMFLOAT4& outRotation
    ) {
        outPosition = PhysicsToWorld(physicsTransform.p);
        outRotation = PhysicsToWorld(physicsTransform.q);
    }
    
    /**
     * DirectX 位置和旋转 → PhysX Transform
     */
    static physx::PxTransform WorldToPhysics(
        const DirectX::XMFLOAT3& worldPosition,
        const DirectX::XMFLOAT4& worldRotation
    ) {
        return physx::PxTransform(
            WorldToPhysics(worldPosition),
            WorldToPhysics(worldRotation)
        );
    }
};

} // namespace outer_wilds
