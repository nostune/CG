#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstddef>

namespace physx {
class PxRenderBuffer;
}

namespace outer_wilds {

class PhysXDebugRenderer {
public:
    struct Vertex;
    struct Constants;

    bool Initialize(ID3D11Device* device);
    void Shutdown();

    bool Render(
        ID3D11DeviceContext* context,
        const physx::PxRenderBuffer& renderBuffer,
        unsigned int width,
        unsigned int height);

    ID3D11ShaderResourceView* GetTexture() const { return m_ShaderResourceView.Get(); }

private:
    bool CreateShaders();
    bool EnsureRenderTarget(unsigned int width, unsigned int height);
    bool EnsureVertexBuffer(std::size_t vertexCount);

    Microsoft::WRL::ComPtr<ID3D11Device> m_Device;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_VertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_PixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_InputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_ConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_VertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_RenderTexture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_RenderTargetView;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_ShaderResourceView;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DepthTexture;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_DepthStencilView;
    std::size_t m_VertexCapacity = 0;
    unsigned int m_Width = 0;
    unsigned int m_Height = 0;
};

} // namespace outer_wilds
