#include "SkyboxRenderer.h"
#include "resources/Shader.h"
#include <vector>
#include <cmath>
#include <iostream>

namespace outer_wilds {

SkyboxRenderer::~SkyboxRenderer() {
    if (m_VertexBuffer) m_VertexBuffer->Release();
    if (m_IndexBuffer) m_IndexBuffer->Release();
    if (m_ConstantBuffer) m_ConstantBuffer->Release();
    if (m_DepthState) m_DepthState->Release();
    if (m_RasterizerState) m_RasterizerState->Release();
}

bool SkyboxRenderer::Initialize(ID3D11Device* device) {
    if (!device) return false;

    if (!CreateSphere(device)) {
        std::cout << "[SkyboxRenderer] Failed to create sphere geometry" << std::endl;
        return false;
    }

    if (!CreateShader(device)) {
        std::cout << "[SkyboxRenderer] Failed to create shader" << std::endl;
        return false;
    }

    if (!CreateRenderStates(device)) {
        std::cout << "[SkyboxRenderer] Failed to create render states" << std::endl;
        return false;
    }

    // 创建常量缓冲区
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(SkyboxCB);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    if (FAILED(device->CreateBuffer(&cbDesc, nullptr, &m_ConstantBuffer))) {
        std::cout << "[SkyboxRenderer] Failed to create constant buffer" << std::endl;
        return false;
    }

    std::cout << "[SkyboxRenderer] Initialized successfully" << std::endl;
    return true;
}

bool SkyboxRenderer::CreateSphere(ID3D11Device* device) {
    // 生成球体网格（用于天空盒内部渲染）
    const int latSegments = 32;
    const int lonSegments = 64;
    
    std::vector<DirectX::XMFLOAT3> vertices;
    std::vector<uint32_t> indices;
    
    // 生成顶点
    for (int lat = 0; lat <= latSegments; lat++) {
        float theta = lat * DirectX::XM_PI / latSegments;
        float sinTheta = std::sin(theta);
        float cosTheta = std::cos(theta);
        
        for (int lon = 0; lon <= lonSegments; lon++) {
            float phi = lon * 2.0f * DirectX::XM_PI / lonSegments;
            float sinPhi = std::sin(phi);
            float cosPhi = std::cos(phi);
            
            // 单位球体顶点（方向向量）
            DirectX::XMFLOAT3 pos;
            pos.x = cosPhi * sinTheta;
            pos.y = cosTheta;
            pos.z = sinPhi * sinTheta;
            
            vertices.push_back(pos);
        }
    }
    
    // 生成索引（反向缠绕，从内部看）
    for (int lat = 0; lat < latSegments; lat++) {
        for (int lon = 0; lon < lonSegments; lon++) {
            int first = lat * (lonSegments + 1) + lon;
            int second = first + lonSegments + 1;
            
            // 反向缠绕（逆时针变顺时针），这样从内部看是正面
            indices.push_back(first);
            indices.push_back(first + 1);
            indices.push_back(second);
            
            indices.push_back(second);
            indices.push_back(first + 1);
            indices.push_back(second + 1);
        }
    }
    
    // 创建顶点缓冲区
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(DirectX::XMFLOAT3));
    vbDesc.Usage = D3D11_USAGE_DEFAULT;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    
    D3D11_SUBRESOURCE_DATA vbData = {};
    vbData.pSysMem = vertices.data();
    
    if (FAILED(device->CreateBuffer(&vbDesc, &vbData, &m_VertexBuffer))) {
        return false;
    }
    
    // 创建索引缓冲区
    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint32_t));
    ibDesc.Usage = D3D11_USAGE_DEFAULT;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    
    D3D11_SUBRESOURCE_DATA ibData = {};
    ibData.pSysMem = indices.data();
    
    if (FAILED(device->CreateBuffer(&ibDesc, &ibData, &m_IndexBuffer))) {
        return false;
    }
    
    m_IndexCount = static_cast<UINT>(indices.size());
    
    std::cout << "[SkyboxRenderer] Created sphere: " << vertices.size() << " vertices, " 
              << indices.size() << " indices" << std::endl;
    
    return true;
}

bool SkyboxRenderer::CreateShader(ID3D11Device* device) {
    m_Shader = std::make_unique<resources::Shader>();
    
    // skybox shader 使用简化的顶点格式（只有位置）
    if (!m_Shader->LoadFromFile(device, "skybox.vs", "skybox.ps", true)) {
        std::cout << "[SkyboxRenderer] Failed to load skybox shader" << std::endl;
        return false;
    }
    
    return true;
}

bool SkyboxRenderer::CreateRenderStates(ID3D11Device* device) {
    // 深度状态：读取深度但不写入，使用 LESS_EQUAL 确保天空盒在最远处
    D3D11_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable = TRUE;
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;  // 不写入深度
    depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;       // <= 可以通过
    
    if (FAILED(device->CreateDepthStencilState(&depthDesc, &m_DepthState))) {
        return false;
    }
    
    // 光栅化状态：禁用背面剔除（从内部看球体）
    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_NONE;  // 不剔除，从内部看
    rastDesc.FrontCounterClockwise = FALSE;
    rastDesc.DepthClipEnable = TRUE;
    
    if (FAILED(device->CreateRasterizerState(&rastDesc, &m_RasterizerState))) {
        return false;
    }
    
    return true;
}

void SkyboxRenderer::Render(
    ID3D11DeviceContext* context,
    const DirectX::XMMATRIX& viewProjection,
    const DirectX::XMFLOAT3& cameraPosition,
    float time
) {
    if (!context || !m_VertexBuffer || !m_IndexBuffer || !m_Shader) {
        return;
    }
    
    // 保存当前状态
    ID3D11DepthStencilState* prevDepthState = nullptr;
    UINT prevStencilRef = 0;
    context->OMGetDepthStencilState(&prevDepthState, &prevStencilRef);
    
    ID3D11RasterizerState* prevRastState = nullptr;
    context->RSGetState(&prevRastState);
    
    // 设置天空盒渲染状态
    context->OMSetDepthStencilState(m_DepthState, 0);
    context->RSSetState(m_RasterizerState);
    
    // 更新常量缓冲区
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context->Map(m_ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        SkyboxCB* cb = static_cast<SkyboxCB*>(mapped.pData);
        cb->viewProjection = DirectX::XMMatrixTranspose(viewProjection);
        cb->cameraPosition = cameraPosition;
        cb->time = time;
        
        // 计算视差偏移
        cb->parallaxOffset.x = cameraPosition.x * m_ParallaxFactor;
        cb->parallaxOffset.y = cameraPosition.y * m_ParallaxFactor;
        cb->parallaxOffset.z = cameraPosition.z * m_ParallaxFactor;
        cb->parallaxFactor = m_ParallaxFactor;
        
        context->Unmap(m_ConstantBuffer, 0);
    }
    
    // 绑定shader
    context->VSSetShader(m_Shader->GetVertexShader(), nullptr, 0);
    context->PSSetShader(m_Shader->GetPixelShader(), nullptr, 0);
    context->IASetInputLayout(m_Shader->GetInputLayout());
    
    // 绑定常量缓冲区
    context->VSSetConstantBuffers(0, 1, &m_ConstantBuffer);
    context->PSSetConstantBuffers(0, 1, &m_ConstantBuffer);
    
    // 绑定顶点/索引缓冲区
    UINT stride = sizeof(DirectX::XMFLOAT3);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, &m_VertexBuffer, &stride, &offset);
    context->IASetIndexBuffer(m_IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    // 绘制
    context->DrawIndexed(m_IndexCount, 0, 0);
    
    // 恢复之前的状态
    context->OMSetDepthStencilState(prevDepthState, prevStencilRef);
    context->RSSetState(prevRastState);
    
    if (prevDepthState) prevDepthState->Release();
    if (prevRastState) prevRastState->Release();
}

} // namespace outer_wilds
