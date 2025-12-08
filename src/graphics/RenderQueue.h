#pragma once

#include "RenderItem.h"
#include "../core/ECS.h"
#include <vector>
#include <algorithm>
#include <d3d11.h>
#include <DirectXMath.h>
#include <cstdint>

namespace outer_wilds {

// 前向声明
namespace resources {
    class Mesh;
    class Material;
}

/**
 * @brief 渲染批次 - 单次DrawCall所需的完整GPU数据
 * 
 * v0.3.0 设计：
 * - 包含GPU资源句柄（VB/IB/Texture/Shader）
 * - 支持状态缓存优化（减少重复绑定）
 * - 使用sortKey实现高效排序
 */
struct RenderBatch {
    // === GPU资源句柄 ===
    ID3D11Buffer* vertexBuffer = nullptr;
    ID3D11Buffer* indexBuffer = nullptr;
    ID3D11ShaderResourceView* texture = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    
    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;
    ID3D11InputLayout* inputLayout = nullptr;
    
    // === 材质数据 ===
    resources::Material* material = nullptr;  // 用于读取albedo等属性
    
    // === 变换矩阵 ===
    DirectX::XMMATRIX worldMatrix;
    
    // === 绘制参数 ===
    uint32_t indexCount = 0;
    uint32_t vertexStride = 0;
    uint32_t vertexOffset = 0;
    
    // === 排序键 ===
    uint64_t sortKey = 0;  // [RenderPass(8)][Shader(8)][Material(8)][Depth(16)][Reserved(24)]
    uint8_t renderPass = 0;  // 0=Opaque, 1=Transparent
    
    /**
     * @brief 计算排序键（自动调用）
     */
    void CalculateSortKey(uint8_t shaderId, uint8_t materialId, uint16_t depth) {
        sortKey = (static_cast<uint64_t>(renderPass) << 56) |
                  (static_cast<uint64_t>(shaderId) << 48) |
                  (static_cast<uint64_t>(materialId) << 40) |
                  (static_cast<uint64_t>(depth) << 24);
    }
    
    bool operator<(const RenderBatch& other) const {
        return sortKey < other.sortKey;
    }
};

/**
 * @brief 渲染队列统计
 */
struct RenderStats {
    uint32_t totalBatches = 0;
    uint32_t drawCalls = 0;
    uint32_t shaderSwitches = 0;
    uint32_t textureSwitches = 0;
    
    void Reset() {
        totalBatches = drawCalls = shaderSwitches = textureSwitches = 0;
    }
};

/**
 * @brief 现代渲染队列 - v0.3.0
 * 
 * 核心功能：
 * 1. 收集（Collect）：从ECS组件构建RenderBatch
 * 2. 排序（Sort）：按sortKey优化绘制顺序
 * 3. 执行（Execute）：带状态缓存的高效DrawCall
 * 4. 手动添加：支持程序化几何体（如调试球体）
 * 
 * 与LightweightRenderQueue的关系：
 * - LightweightRenderQueue：只存Entity ID，适合简单场景
 * - RenderQueue：存完整GPU数据，支持状态优化和手动添加
 * - 两者可共存，逐步迁移
 */
class RenderQueue {
public:
    RenderQueue() = default;
    ~RenderQueue() = default;

    /**
     * @brief 清空队列
     */
    void Clear() {
        m_Batches.clear();
        m_Stats.Reset();
    }

    /**
     * @brief 从ECS收集渲染批次
     * @param registry ECS注册表
     * @param cameraPos 相机位置（用于计算深度）
     */
    void CollectFromECS(entt::registry& registry, const DirectX::XMFLOAT3& cameraPos);

    /**
     * @brief 手动添加批次（用于程序化几何体）
     * @param batch 渲染批次
     * 
     * 用例：在main.cpp中绘制调试球体
     * @code
     * RenderBatch debugSphere;
     * debugSphere.vertexBuffer = sphereVB;
     * debugSphere.texture = whiteTexture;
     * debugSphere.worldMatrix = sphereTransform;
     * renderQueue.AddBatch(debugSphere);
     * @endcode
     */
    void AddBatch(const RenderBatch& batch) {
        m_Batches.push_back(batch);
    }

    /**
     * @brief 排序批次（减少状态切换）
     */
    void Sort() {
        std::sort(m_Batches.begin(), m_Batches.end());
    }

    /**
     * @brief 执行绘制（带状态缓存）
     * @param context D3D11设备上下文
     * @param perObjectCB PerObject常量缓冲区
     */
    void Execute(ID3D11DeviceContext* context, ID3D11Buffer* perObjectCB);

    /**
     * @brief 获取统计信息
     */
    const RenderStats& GetStats() const { return m_Stats; }
    
    /**
     * @brief 获取批次数量
     */
    size_t GetBatchCount() const { return m_Batches.size(); }
    
    /**
     * @brief 队列是否为空
     */
    bool Empty() const { return m_Batches.empty(); }

private:
    std::vector<RenderBatch> m_Batches;
    RenderStats m_Stats;
};

} // namespace outer_wilds

