#pragma once
#include <DirectXMath.h>
#include <vector>
#include <cstdint>
#include <d3d11.h>

namespace outer_wilds {
namespace resources {

struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 texCoord;
    DirectX::XMFLOAT3 tangent;
};

class Mesh {
public:
    Mesh() = default;
    ~Mesh() = default;

    void SetVertices(const std::vector<Vertex>& vertices) { m_Vertices = vertices; }
    void SetIndices(const std::vector<uint32_t>& indices) { m_Indices = indices; }
    
    const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
    const std::vector<uint32_t>& GetIndices() const { return m_Indices; }
    uint32_t GetIndexCount() const { return static_cast<uint32_t>(m_Indices.size()); }
    
    // GPU resource creation
    void CreateGPUBuffers(ID3D11Device* device);
    
    // GPU resource handles (to be filled by renderer)
    void* vertexBuffer = nullptr;
    void* indexBuffer = nullptr;

private:
    std::vector<Vertex> m_Vertices;
    std::vector<uint32_t> m_Indices;
};

} // namespace resources
} // namespace outer_wilds
