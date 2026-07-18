#include "PhysXDebugRenderer.h"

#include "../../core/DebugManager.h"

#include <PxPhysicsAPI.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace outer_wilds {

using Microsoft::WRL::ComPtr;
using namespace DirectX;

struct PhysXDebugRenderer::Vertex {
    XMFLOAT3 position;
    XMFLOAT4 color;
};

struct PhysXDebugRenderer::Constants {
    XMFLOAT4X4 viewProjection;
};

namespace {

XMFLOAT4 DecodeColor(physx::PxU32 argb) {
    constexpr float scale = 1.0f / 255.0f;
    return {
        static_cast<float>((argb >> 16) & 0xff) * scale,
        static_cast<float>((argb >> 8) & 0xff) * scale,
        static_cast<float>(argb & 0xff) * scale,
        static_cast<float>((argb >> 24) & 0xff) * scale
    };
}

XMFLOAT3 Position(const physx::PxVec3& value) {
    return {value.x, value.y, value.z};
}

void AddLine(
    std::vector<PhysXDebugRenderer::Vertex>& vertices,
    const physx::PxVec3& start,
    physx::PxU32 startColor,
    const physx::PxVec3& end,
    physx::PxU32 endColor) {
    vertices.push_back({Position(start), DecodeColor(startColor)});
    vertices.push_back({Position(end), DecodeColor(endColor)});
}

} // namespace

bool PhysXDebugRenderer::Initialize(ID3D11Device* device) {
    if (!device) {
        return false;
    }
    m_Device = device;
    return CreateShaders();
}

void PhysXDebugRenderer::Shutdown() {
    m_DepthStencilView.Reset();
    m_DepthTexture.Reset();
    m_ShaderResourceView.Reset();
    m_RenderTargetView.Reset();
    m_RenderTexture.Reset();
    m_VertexBuffer.Reset();
    m_ConstantBuffer.Reset();
    m_InputLayout.Reset();
    m_PixelShader.Reset();
    m_VertexShader.Reset();
    m_Device.Reset();
    m_VertexCapacity = 0;
    m_Width = 0;
    m_Height = 0;
}

bool PhysXDebugRenderer::CreateShaders() {
    static constexpr char shaderSource[] = R"(
cbuffer DebugConstants : register(b0) {
    float4x4 viewProjection;
};

struct VSInput {
    float3 position : POSITION;
    float4 color : COLOR0;
};

struct PSInput {
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

PSInput VSMain(VSInput input) {
    PSInput output;
    output.position = mul(float4(input.position, 1.0f), viewProjection);
    output.color = input.color;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET {
    return input.color;
}
)";

    ComPtr<ID3DBlob> vertexBlob;
    ComPtr<ID3DBlob> pixelBlob;
    ComPtr<ID3DBlob> errors;
    HRESULT result = D3DCompile(
        shaderSource, std::strlen(shaderSource), "PhysXDebugRenderer", nullptr, nullptr,
        "VSMain", "vs_5_0", 0, 0, &vertexBlob, &errors);
    if (FAILED(result)) {
        const char* message = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown shader error";
        DebugManager::GetInstance().Error("PhysXDebugView", message);
        return false;
    }

    errors.Reset();
    result = D3DCompile(
        shaderSource, std::strlen(shaderSource), "PhysXDebugRenderer", nullptr, nullptr,
        "PSMain", "ps_5_0", 0, 0, &pixelBlob, &errors);
    if (FAILED(result)) {
        const char* message = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown shader error";
        DebugManager::GetInstance().Error("PhysXDebugView", message);
        return false;
    }

    if (FAILED(m_Device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &m_VertexShader)) ||
        FAILED(m_Device->CreatePixelShader(
            pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize(), nullptr, &m_PixelShader))) {
        DebugManager::GetInstance().Error("PhysXDebugView", "Failed to create debug shaders");
        return false;
    }

    const D3D11_INPUT_ELEMENT_DESC inputElements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    if (FAILED(m_Device->CreateInputLayout(
            inputElements, ARRAYSIZE(inputElements), vertexBlob->GetBufferPointer(),
            vertexBlob->GetBufferSize(), &m_InputLayout))) {
        DebugManager::GetInstance().Error("PhysXDebugView", "Failed to create input layout");
        return false;
    }

    D3D11_BUFFER_DESC constants = {};
    constants.ByteWidth = sizeof(Constants);
    constants.Usage = D3D11_USAGE_DYNAMIC;
    constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(m_Device->CreateBuffer(&constants, nullptr, &m_ConstantBuffer));
}

bool PhysXDebugRenderer::EnsureRenderTarget(unsigned int width, unsigned int height) {
    width = (std::max)(width, 1u);
    height = (std::max)(height, 1u);
    if (m_RenderTexture && width == m_Width && height == m_Height) {
        return true;
    }

    m_DepthStencilView.Reset();
    m_DepthTexture.Reset();
    m_ShaderResourceView.Reset();
    m_RenderTargetView.Reset();
    m_RenderTexture.Reset();

    D3D11_TEXTURE2D_DESC colorDesc = {};
    colorDesc.Width = width;
    colorDesc.Height = height;
    colorDesc.MipLevels = 1;
    colorDesc.ArraySize = 1;
    colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Usage = D3D11_USAGE_DEFAULT;
    colorDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(m_Device->CreateTexture2D(&colorDesc, nullptr, &m_RenderTexture)) ||
        FAILED(m_Device->CreateRenderTargetView(m_RenderTexture.Get(), nullptr, &m_RenderTargetView)) ||
        FAILED(m_Device->CreateShaderResourceView(m_RenderTexture.Get(), nullptr, &m_ShaderResourceView))) {
        return false;
    }

    D3D11_TEXTURE2D_DESC depthDesc = colorDesc;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(m_Device->CreateTexture2D(&depthDesc, nullptr, &m_DepthTexture)) ||
        FAILED(m_Device->CreateDepthStencilView(m_DepthTexture.Get(), nullptr, &m_DepthStencilView))) {
        return false;
    }

    m_Width = width;
    m_Height = height;
    return true;
}

bool PhysXDebugRenderer::EnsureVertexBuffer(std::size_t vertexCount) {
    if (m_VertexBuffer && vertexCount <= m_VertexCapacity) {
        return true;
    }

    m_VertexCapacity = (std::max)(vertexCount, static_cast<std::size_t>(4096));
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = static_cast<UINT>(m_VertexCapacity * sizeof(Vertex));
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    m_VertexBuffer.Reset();
    return SUCCEEDED(m_Device->CreateBuffer(&desc, nullptr, &m_VertexBuffer));
}

bool PhysXDebugRenderer::Render(
    ID3D11DeviceContext* context,
    const physx::PxRenderBuffer& renderBuffer,
    unsigned int width,
    unsigned int height) {
    if (!context || !EnsureRenderTarget(width, height)) {
        return false;
    }

    std::vector<Vertex> vertices;
    vertices.reserve(
        static_cast<std::size_t>(renderBuffer.getNbLines()) * 2 +
        static_cast<std::size_t>(renderBuffer.getNbTriangles()) * 6);

    const auto* lines = renderBuffer.getLines();
    for (physx::PxU32 index = 0; index < renderBuffer.getNbLines(); ++index) {
        AddLine(vertices, lines[index].pos0, lines[index].color0, lines[index].pos1, lines[index].color1);
    }
    const auto* triangles = renderBuffer.getTriangles();
    for (physx::PxU32 index = 0; index < renderBuffer.getNbTriangles(); ++index) {
        const auto& triangle = triangles[index];
        AddLine(vertices, triangle.pos0, triangle.color0, triangle.pos1, triangle.color1);
        AddLine(vertices, triangle.pos1, triangle.color1, triangle.pos2, triangle.color2);
        AddLine(vertices, triangle.pos2, triangle.color2, triangle.pos0, triangle.color0);
    }

    ComPtr<ID3D11RenderTargetView> previousRenderTarget;
    ComPtr<ID3D11DepthStencilView> previousDepthStencil;
    context->OMGetRenderTargets(1, &previousRenderTarget, &previousDepthStencil);
    UINT previousViewportCount = 1;
    D3D11_VIEWPORT previousViewport = {};
    context->RSGetViewports(&previousViewportCount, &previousViewport);

    ID3D11ShaderResourceView* nullResource = nullptr;
    context->PSSetShaderResources(0, 1, &nullResource);
    ID3D11RenderTargetView* renderTarget = m_RenderTargetView.Get();
    context->OMSetRenderTargets(1, &renderTarget, m_DepthStencilView.Get());
    const float clearColor[] = {0.025f, 0.030f, 0.038f, 1.0f};
    context->ClearRenderTargetView(m_RenderTargetView.Get(), clearColor);
    context->ClearDepthStencilView(m_DepthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    D3D11_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(m_Width), static_cast<float>(m_Height), 0.0f, 1.0f};
    context->RSSetViewports(1, &viewport);

    if (!vertices.empty() && EnsureVertexBuffer(vertices.size())) {
        XMFLOAT3 minimum(
            std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        XMFLOAT3 maximum(
            std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
        for (const auto& vertex : vertices) {
            minimum.x = (std::min)(minimum.x, vertex.position.x);
            minimum.y = (std::min)(minimum.y, vertex.position.y);
            minimum.z = (std::min)(minimum.z, vertex.position.z);
            maximum.x = (std::max)(maximum.x, vertex.position.x);
            maximum.y = (std::max)(maximum.y, vertex.position.y);
            maximum.z = (std::max)(maximum.z, vertex.position.z);
        }

        const XMVECTOR minVector = XMLoadFloat3(&minimum);
        const XMVECTOR maxVector = XMLoadFloat3(&maximum);
        const XMVECTOR center = XMVectorScale(XMVectorAdd(minVector, maxVector), 0.5f);
        const float radius = (std::max)(XMVectorGetX(XMVector3Length(XMVectorSubtract(maxVector, minVector))) * 0.5f, 1.0f);
        const XMVECTOR eye = XMVectorAdd(center, XMVectorSet(radius * 1.5f, radius * 1.1f, -radius * 1.5f, 0.0f));
        const XMMATRIX view = XMMatrixLookAtLH(eye, center, XMVectorSet(0, 1, 0, 0));
        const XMMATRIX projection = XMMatrixPerspectiveFovLH(
            XMConvertToRadians(45.0f), static_cast<float>(m_Width) / static_cast<float>(m_Height), 0.05f, radius * 12.0f);
        Constants constants = {};
        XMStoreFloat4x4(&constants.viewProjection, XMMatrixTranspose(XMMatrixMultiply(view, projection)));

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(context->Map(m_VertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(Vertex));
            context->Unmap(m_VertexBuffer.Get(), 0);
        }
        if (SUCCEEDED(context->Map(m_ConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &constants, sizeof(constants));
            context->Unmap(m_ConstantBuffer.Get(), 0);
        }

        const UINT stride = sizeof(Vertex);
        const UINT offset = 0;
        ID3D11Buffer* vertexBuffer = m_VertexBuffer.Get();
        ID3D11Buffer* constantBuffer = m_ConstantBuffer.Get();
        context->IASetInputLayout(m_InputLayout.Get());
        context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        context->VSSetShader(m_VertexShader.Get(), nullptr, 0);
        context->VSSetConstantBuffers(0, 1, &constantBuffer);
        context->PSSetShader(m_PixelShader.Get(), nullptr, 0);
        context->Draw(static_cast<UINT>(vertices.size()), 0);
    }

    ID3D11RenderTargetView* previousTarget = previousRenderTarget.Get();
    context->OMSetRenderTargets(1, &previousTarget, previousDepthStencil.Get());
    if (previousViewportCount > 0) {
        context->RSSetViewports(1, &previousViewport);
    }
    return true;
}

} // namespace outer_wilds
