#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <cstdint>

namespace outer_wilds {

/**
 * @brief 渲染批次 - 单次DrawCall所需的完整数据
 * 
 * 设计目标：
 * 1. 封装GPU绘制所需的所有状态（Shader/Texture/Buffer/Matrix）
 * 2. 通过sortKey优化渲染顺序，减少状态切换
 * 3. 支持手动添加和自动收集两种模式
 */
struct RenderBatch {
    // === GPU资源句柄 ===
    ID3D11Buffer* vertexBuffer = nullptr;
    ID3D11Buffer* indexBuffer = nullptr;
    ID3D11ShaderResourceView* texture = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    
    // === Shader状态 ===
    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;
    ID3D11InputLayout* inputLayout = nullptr;
    
    // === 变换矩阵 ===
    DirectX::XMMATRIX worldMatrix;
    
    // === 绘制参数 ===
    uint32_t indexCount = 0;
    uint32_t vertexStride = 0;
    uint32_t vertexOffset = 0;
    
    // === 排序键（用于优化渲染顺序） ===
    uint64_t sortKey = 0;
    uint8_t shaderId = 0;
    uint8_t materialId = 0;
    uint16_t depth = 0;
    uint8_t renderPass = 0;  // 0=Opaque, 1=Transparent
    
    /**
     * @brief 计算排序键
     * 布局: [RenderPass(8)][Shader(8)][Material(8)][Depth(16)][Reserved(24)]
     */
    void CalculateSortKey() {
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
 * @brief 渲染队列统计信息
 */
struct RenderQueueStats {
    uint32_t totalBatches = 0;
    uint32_t drawCalls = 0;
    uint32_t shaderSwitches = 0;
    uint32_t textureSwitches = 0;
    
    void Reset() {
        totalBatches = drawCalls = shaderSwitches = textureSwitches = 0;
    }
};

} // namespace outer_wilds
