#include "RenderBackend.h"
#include "../core/DebugManager.h"
#include <d3d11.h>
#include <dxgi.h>
#include <DirectXMath.h>
#include <iostream>

namespace outer_wilds {

bool RenderBackend::Initialize(void* hwnd, int width, int height) {
    m_Width = width;
    m_Height = height;

    // Create device and swap chain
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = 1;
    swapChainDesc.BufferDesc.Width = width;
    swapChainDesc.BufferDesc.Height = height;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = static_cast<HWND>(hwnd);
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.Windowed = TRUE;

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        &featureLevel,
        1,
        D3D11_SDK_VERSION,
        &swapChainDesc,
        m_SwapChain.GetAddressOf(),
        m_Device.GetAddressOf(),
        nullptr,
        m_Context.GetAddressOf()
    );

    if (FAILED(hr)) {
        std::cerr << "Failed to create D3D11 device and swap chain" << std::endl;
        return false;
    }

    // Create render target view
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &backBuffer);
    if (FAILED(hr)) {
        std::cerr << "Failed to get back buffer" << std::endl;
        return false;
    }

    hr = m_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_RenderTargetView.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "Failed to create render target view" << std::endl;
        return false;
    }

    // Create depth stencil view
    D3D11_TEXTURE2D_DESC depthStencilDesc = {};
    depthStencilDesc.Width = width;
    depthStencilDesc.Height = height;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.ArraySize = 1;
    depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.SampleDesc.Quality = 0;
    depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
    depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ComPtr<ID3D11Texture2D> depthStencil;
    hr = m_Device->CreateTexture2D(&depthStencilDesc, nullptr, depthStencil.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "Failed to create depth stencil texture" << std::endl;
        return false;
    }

    hr = m_Device->CreateDepthStencilView(depthStencil.Get(), nullptr, m_DepthStencilView.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "Failed to create depth stencil view" << std::endl;
        return false;
    }

    // Set render targets
    m_Context->OMSetRenderTargets(1, m_RenderTargetView.GetAddressOf(), m_DepthStencilView.Get());

    // Set viewport
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    m_Context->RSSetViewports(1, &viewport);

    // Create rasterizer state
    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_BACK;
    rasterizerDesc.FrontCounterClockwise = FALSE;
    rasterizerDesc.DepthBias = 0;
    rasterizerDesc.DepthBiasClamp = 0.0f;
    rasterizerDesc.SlopeScaledDepthBias = 0.0f;
    rasterizerDesc.DepthClipEnable = TRUE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.MultisampleEnable = FALSE;
    rasterizerDesc.AntialiasedLineEnable = FALSE;

    hr = m_Device->CreateRasterizerState(&rasterizerDesc, &m_RasterizerState);
    if (FAILED(hr)) {
        std::cerr << "Failed to create rasterizer state" << std::endl;
        return false;
    }

    m_Context->RSSetState(m_RasterizerState.Get());

    // Create depth stencil state
    D3D11_DEPTH_STENCIL_DESC depthStencilStateDesc = {};
    depthStencilStateDesc.DepthEnable = TRUE;
    depthStencilStateDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthStencilStateDesc.DepthFunc = D3D11_COMPARISON_LESS;
    depthStencilStateDesc.StencilEnable = FALSE;

    hr = m_Device->CreateDepthStencilState(&depthStencilStateDesc, &m_DepthStencilState);
    if (FAILED(hr)) {
        std::cerr << "Failed to create depth stencil state" << std::endl;
        return false;
    }

    m_Context->OMSetDepthStencilState(m_DepthStencilState.Get(), 0);

    DebugManager::GetInstance().Log("RenderBackend", "RenderBackend initialized successfully");
    return true;
}

void RenderBackend::Shutdown() {
    m_DepthStencilState.Reset();
    m_RasterizerState.Reset();
    m_DepthStencilView.Reset();
    m_RenderTargetView.Reset();
    m_SwapChain.Reset();
    m_Context.Reset();
    m_Device.Reset();
}

void RenderBackend::BeginFrame() {
    // This is now handled by RenderSystem
    // float clearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
    // m_Context->ClearRenderTargetView(m_RenderTargetView.Get(), clearColor);
    // m_Context->ClearDepthStencilView(m_DepthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void RenderBackend::EndFrame() {
    // Nothing to do here for now
}

void RenderBackend::Present() {
    m_SwapChain->Present(1, 0);
}

void RenderBackend::ResizeBuffers(int width, int height) {
    if (width == m_Width && height == m_Height) return;

    m_Width = width;
    m_Height = height;

    // Release render target view
    m_RenderTargetView.Reset();
    m_DepthStencilView.Reset();

    // Resize swap chain
    HRESULT hr = m_SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        std::cerr << "Failed to resize swap chain buffers" << std::endl;
        return;
    }

    // Recreate render target view
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &backBuffer);
    if (FAILED(hr)) {
        std::cerr << "Failed to get resized back buffer" << std::endl;
        return;
    }

    hr = m_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_RenderTargetView.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "Failed to create resized render target view" << std::endl;
        return;
    }

    // Recreate depth stencil view
    D3D11_TEXTURE2D_DESC depthStencilDesc = {};
    depthStencilDesc.Width = width;
    depthStencilDesc.Height = height;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.ArraySize = 1;
    depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.SampleDesc.Quality = 0;
    depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
    depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ComPtr<ID3D11Texture2D> depthStencil;
    hr = m_Device->CreateTexture2D(&depthStencilDesc, nullptr, depthStencil.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "Failed to create resized depth stencil texture" << std::endl;
        return;
    }

    hr = m_Device->CreateDepthStencilView(depthStencil.Get(), nullptr, m_DepthStencilView.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "Failed to create resized depth stencil view" << std::endl;
        return;
    }

    // Set render targets
    m_Context->OMSetRenderTargets(1, m_RenderTargetView.GetAddressOf(), m_DepthStencilView.Get());

    // Set viewport
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    m_Context->RSSetViewports(1, &viewport);
}

} // namespace outer_wilds