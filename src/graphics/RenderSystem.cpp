#include "RenderSystem.h"
#include "../scene/Scene.h"
#include "../scene/SceneManager.h"
#include "components/CameraComponent.h"
#include "../scene/components/TransformComponent.h"
#include "../core/DebugManager.h"
#include "../core/Engine.h"
#include "../ui/UISystem.h"
#include <DirectXMath.h>
#include <d3d11.h>

namespace outer_wilds {

void RenderSystem::Initialize(SceneManager* sceneManager) {
    m_SceneManager = sceneManager;
    m_Backend = std::make_unique<RenderBackend>();
}

void RenderSystem::Update(float deltaTime, entt::registry& registry) {
    m_UpdateCounter++;
    bool shouldDebug = (m_UpdateCounter % 100 == 0);
    
    // 调试：确认Update被调用
    static int updateDebugCount = 0;
    if (updateDebugCount < 3) {
        std::cout << "[RenderSystem::Update] IMMEDIATE: Called #" << (++updateDebugCount) 
                  << ", frame=" << m_UpdateCounter << std::endl;
    }
    
    if (!m_Backend) return;

    // Get current active scene
    auto scenePtr = m_SceneManager->GetActiveScene();
    if (!scenePtr) {
        if (shouldDebug) DebugManager::GetInstance().Log("RenderSystem", "Active scene is null");
        return;
    }

    // Find active camera
    auto camera = FindActiveCamera(scenePtr->GetRegistry());
    if (!camera) {
        if (shouldDebug) DebugManager::GetInstance().Log("RenderSystem", "No active camera found");
        return;
    }

    // Render all entities
    RenderScene(camera, scenePtr->GetRegistry(), shouldDebug);

    // Render UI (after scene, before Present)
    if (auto uiSystem = Engine::GetInstance().GetUISystem()) {
        uiSystem->Render();
    }

    m_Backend->EndFrame();
    m_Backend->Present();
}

bool RenderSystem::InitializeBackend(void* hwnd, int width, int height) {
    if (!m_Backend) {
        m_Backend = std::make_unique<RenderBackend>();
    }
    return m_Backend->Initialize(hwnd, width, height);
}

void RenderSystem::RenderScene(components::CameraComponent* camera, entt::registry& registry, bool shouldDebug) {
    static int renderCount = 0;
    if (renderCount++ < 3) {
        std::cout << "[RenderSystem::RenderScene] IMMEDIATE: Called #" << renderCount << std::endl;
    }
    
    if (!m_Backend || !camera) return;

    auto device = static_cast<ID3D11Device*>(m_Backend->GetDevice());
    auto context = static_cast<ID3D11DeviceContext*>(m_Backend->GetContext());
    if (!device || !context) return;

    // Set render targets
    auto renderTargetView = static_cast<ID3D11RenderTargetView*>(m_Backend->GetRenderTargetView());
    auto depthStencilView = static_cast<ID3D11DepthStencilView*>(m_Backend->GetDepthStencilView());
    if (!renderTargetView || !depthStencilView) return;
    
    context->OMSetRenderTargets(1, &renderTargetView, depthStencilView);

    // Set viewport
    D3D11_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = 1280.0f;
    viewport.Height = 720.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);

    // Clear render targets
    float clearColor[4] = { 0.529f, 0.808f, 0.922f, 1.0f }; // Sky blue
    context->ClearRenderTargetView(renderTargetView, clearColor);
    context->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    // Setup render states
    static ID3D11DepthStencilState* s_depthState = nullptr;
    static ID3D11RasterizerState* s_rastState = nullptr;

    if (!s_depthState) {
        D3D11_DEPTH_STENCIL_DESC depthDesc = {};
        depthDesc.DepthEnable = TRUE;
        depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depthDesc.DepthFunc = D3D11_COMPARISON_LESS;
        device->CreateDepthStencilState(&depthDesc, &s_depthState);
    }
    if (!s_rastState) {
        D3D11_RASTERIZER_DESC rastDesc = {};
        rastDesc.FillMode = D3D11_FILL_SOLID;
        rastDesc.CullMode = D3D11_CULL_BACK;
        rastDesc.FrontCounterClockwise = FALSE;
        rastDesc.DepthClipEnable = TRUE;
        device->CreateRasterizerState(&rastDesc, &s_rastState);
    }

    context->OMSetDepthStencilState(s_depthState, 0);
    context->RSSetState(s_rastState);

    // Setup camera matrices
    DirectX::XMVECTOR eyePos = DirectX::XMLoadFloat3(&camera->position);
    DirectX::XMVECTOR lookAt = DirectX::XMLoadFloat3(&camera->target);
    DirectX::XMVECTOR upDir = DirectX::XMLoadFloat3(&camera->up);

    DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eyePos, lookAt, upDir);
    DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovLH(
        camera->fov * DirectX::XM_PI / 180.0f,
        camera->aspectRatio,
        camera->nearPlane,
        camera->farPlane
    );
    DirectX::XMMATRIX viewProjection = DirectX::XMMatrixMultiply(view, projection);

    // Create/update constant buffers
    static ID3D11Buffer* s_perFrameCB = nullptr;

    if (!s_perFrameCB) {
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.ByteWidth = sizeof(DirectX::XMMATRIX);
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(&cbDesc, nullptr, &s_perFrameCB);
    }
    if (!m_PerObjectCB) {
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.ByteWidth = 80;  // world(64) + color(16)
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(&cbDesc, nullptr, &m_PerObjectCB);
    }

    // Update per-frame constant buffer (view-projection matrix)
    if (s_perFrameCB) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(context->Map(s_perFrameCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            DirectX::XMMATRIX vpTransposed = DirectX::XMMatrixTranspose(viewProjection);
            memcpy(mapped.pData, &vpTransposed, sizeof(DirectX::XMMATRIX));
            context->Unmap(s_perFrameCB, 0);
        }
        context->VSSetConstantBuffers(0, 1, &s_perFrameCB);
    }

    // ============================================
    // v0.3.0 渲染管线：Collect -> Sort -> Execute
    // ============================================
    
    // 调试（已禁用）
    // static int pipelineDebugCount = 0;
    // if (pipelineDebugCount++ < 3) {
    //     std::cout << "[RenderSystem] Starting RenderQueue pipeline" << std::endl;
    // }
    
    // 1. 清空并收集批次
    m_RenderQueue.Clear();
    m_RenderQueue.CollectFromECS(registry, camera->position);
    
    // 2. 排序（优化状态切换）
    m_RenderQueue.Sort();
    
    // 3. 执行绘制（带状态缓存）
    m_RenderQueue.Execute(context, m_PerObjectCB);
    
    // 4. 调试日志（已禁用）
    // if (shouldDebug) {
    //     const auto& stats = m_RenderQueue.GetStats();
    //     DebugManager::GetInstance().Log("RenderSystem", "...");
    // }
}

components::CameraComponent* RenderSystem::FindActiveCamera(entt::registry& registry) {
    components::CameraComponent* activeCamera = nullptr;

    // Find any entity with an active CameraComponent
    auto view = registry.view<components::CameraComponent, TransformComponent>();
    view.each([&](auto entity, auto& cam, auto& trans) {
        if (cam.isActive) {
            cam.position = trans.position;
            activeCamera = &cam;
        }
    });

    return activeCamera;
}

} // namespace outer_wilds
