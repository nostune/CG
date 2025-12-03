#include "Mesh.h"
#include <d3d11.h>
#include "core/DebugManager.h"

namespace outer_wilds {
namespace resources {

void Mesh::CreateGPUBuffers(ID3D11Device* device) {
    if (!device || m_Vertices.empty()) {
        DebugManager::GetInstance().Log("Mesh", "CreateGPUBuffers failed: device null or no vertices");
        return;
    }

    // Create vertex buffer
    D3D11_BUFFER_DESC vertexBufferDesc = {};
    vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    vertexBufferDesc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * m_Vertices.size());
    vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vertexBufferDesc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA vertexData = {};
    vertexData.pSysMem = m_Vertices.data();

    ID3D11Buffer* vb = nullptr;
    HRESULT hr = device->CreateBuffer(&vertexBufferDesc, &vertexData, &vb);
    if (FAILED(hr)) {
        DebugManager::GetInstance().Log("Mesh", "Failed to create vertex buffer, HRESULT: " + std::to_string(hr));
        return;
    }
    this->vertexBuffer = vb;
    DebugManager::GetInstance().Log("Mesh", "Vertex buffer created successfully with " + std::to_string(m_Vertices.size()) + " vertices");

    // Create index buffer if indices exist
    if (!m_Indices.empty()) {
        D3D11_BUFFER_DESC indexBufferDesc = {};
        indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
        indexBufferDesc.ByteWidth = static_cast<UINT>(sizeof(uint32_t) * m_Indices.size());
        indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        indexBufferDesc.CPUAccessFlags = 0;

        D3D11_SUBRESOURCE_DATA indexData = {};
        indexData.pSysMem = m_Indices.data();

        ID3D11Buffer* ib = nullptr;
        hr = device->CreateBuffer(&indexBufferDesc, &indexData, &ib);
        if (FAILED(hr)) {
            DebugManager::GetInstance().Log("Mesh", "Failed to create index buffer, HRESULT: " + std::to_string(hr));
            return;
        }
        this->indexBuffer = ib;
        DebugManager::GetInstance().Log("Mesh", "Index buffer created successfully with " + std::to_string(m_Indices.size()) + " indices");
    } else {
        DebugManager::GetInstance().Log("Mesh", "No indices provided, skipping index buffer creation");
    }
}

} // namespace resources
} // namespace outer_wilds