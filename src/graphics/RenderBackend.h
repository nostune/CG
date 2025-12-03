#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

using Microsoft::WRL::ComPtr;

namespace outer_wilds {

class RenderBackend {
public:
    RenderBackend() = default;
    ~RenderBackend() = default;

    bool Initialize(void* hwnd, int width, int height);
    void Shutdown();
    
    void BeginFrame();
    void EndFrame();
    void Present();
    
    ID3D11Device* GetDevice() { return m_Device.Get(); }
    ID3D11DeviceContext* GetContext() { return m_Context.Get(); }
    ID3D11RenderTargetView* GetRenderTargetView() { return m_RenderTargetView.Get(); }
    ID3D11DepthStencilView* GetDepthStencilView() { return m_DepthStencilView.Get(); }
    
    void ResizeBuffers(int width, int height);

private:
    ComPtr<ID3D11Device> m_Device;
    ComPtr<ID3D11DeviceContext> m_Context;
    ComPtr<IDXGISwapChain> m_SwapChain;
    ComPtr<ID3D11RenderTargetView> m_RenderTargetView;
    ComPtr<ID3D11DepthStencilView> m_DepthStencilView;
    ComPtr<ID3D11RasterizerState> m_RasterizerState;
    ComPtr<ID3D11DepthStencilState> m_DepthStencilState;
    
    int m_Width = 0;
    int m_Height = 0;
};

} // namespace outer_wilds
