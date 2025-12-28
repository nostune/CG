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
#include <iostream>

namespace outer_wilds {

void RenderSystem::Initialize(SceneManager* sceneManager) {
    m_SceneManager = sceneManager;
    m_Backend = std::make_unique<RenderBackend>();
    m_SkyboxRenderer = std::make_unique<SkyboxRenderer>();
}

void RenderSystem::Update(float deltaTime, entt::registry& registry) {
    m_UpdateCounter++;
    m_Time += deltaTime;  // 累积时间
    bool shouldDebug = (m_UpdateCounter % 100 == 0);
    
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

    // Clear render targets (深空黑色背景)
    float clearColor[4] = { 0.01f, 0.01f, 0.02f, 1.0f }; // 深空背景
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
        rastDesc.CullMode = D3D11_CULL_NONE;  // 暂时禁用背面剔除来调试
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

    // ============================================
    // 1. 渲染星空天空盒（最先渲染，深度最远）
    // ============================================
    if (m_SkyboxRenderer) {
        // 初始化天空盒（如果尚未初始化）
        static bool skyboxInitialized = false;
        if (!skyboxInitialized) {
            if (m_SkyboxRenderer->Initialize(device)) {
                skyboxInitialized = true;
                std::cout << "[RenderSystem] Skybox initialized" << std::endl;
            }
        }
        
        if (skyboxInitialized) {
            m_SkyboxRenderer->Render(context, viewProjection, camera->position, m_Time);
        }
    }

    // ============================================
    // 2. 获取太阳位置（用于动态光照）
    // ============================================
    DirectX::XMFLOAT3 sunPosition = GetSunPosition(registry);
    
    // Create/update constant buffers
    // 扩展的 PerFrameBuffer 结构 - 必须与 HLSL 的 cbuffer 完全匹配！
    // HLSL cbuffer 有 16 字节对齐要求
    struct PerFrameData {
        DirectX::XMMATRIX viewProjection;  // 64 bytes (4x4 matrix)
        DirectX::XMFLOAT3 cameraPosition;  // 12 bytes
        float time;                         // 4 bytes  → 总共 80 bytes
        DirectX::XMFLOAT3 sunPosition;     // 12 bytes
        float sunIntensity;                // 4 bytes  → 总共 96 bytes
        DirectX::XMFLOAT3 sunColor;        // 12 bytes
        float ambientStrength;             // 4 bytes  → 总共 112 bytes
    };
    
    static_assert(sizeof(PerFrameData) == 112, "PerFrameData must be 112 bytes to match HLSL cbuffer");
    
    static ID3D11Buffer* s_perFrameCB = nullptr;

    if (!s_perFrameCB) {
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.ByteWidth = sizeof(PerFrameData);
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(&cbDesc, nullptr, &s_perFrameCB);
    }
    if (!m_PerObjectCB) {
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.ByteWidth = 96;  // world(64) + color(16) + objectCenter(12) + isSphere(4) = 96
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(&cbDesc, nullptr, &m_PerObjectCB);
    }

    // Update per-frame constant buffer
    if (s_perFrameCB) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(context->Map(s_perFrameCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            PerFrameData* data = static_cast<PerFrameData*>(mapped.pData);
            data->viewProjection = DirectX::XMMatrixTranspose(viewProjection);
            data->cameraPosition = camera->position;
            data->time = m_Time;
            data->sunPosition = sunPosition;
            data->sunIntensity = 1.2f;  // 太阳光强度
            data->sunColor = { 1.0f, 0.95f, 0.85f };  // 暖黄色太阳光
            data->ambientStrength = 0.25f;  // 环境光强度
            
            context->Unmap(s_perFrameCB, 0);
        }
        context->VSSetConstantBuffers(0, 1, &s_perFrameCB);
        context->PSSetConstantBuffers(0, 1, &s_perFrameCB);
    }

    // ============================================
    // 3. 渲染场景物体
    // ============================================
    
    // 1. 清空并收集批次（传入 sunPosition 用于计算光照方向）
    m_RenderQueue.Clear();
    m_RenderQueue.CollectFromECS(registry, camera->position, sunPosition);
    
    // 2. 排序（优化状态切换）
    m_RenderQueue.Sort();
    
    // 3. 执行绘制（带状态缓存）
    m_RenderQueue.Execute(context, m_PerObjectCB, sunPosition);
}

DirectX::XMFLOAT3 RenderSystem::GetSunPosition(entt::registry& registry) {
    // 默认太阳位置（如果没有设置太阳实体）
    DirectX::XMFLOAT3 defaultSunPos = { 0.0f, 0.0f, 0.0f };
    
    if (m_SunEntity != entt::null && registry.valid(m_SunEntity)) {
        auto* transform = registry.try_get<TransformComponent>(m_SunEntity);
        if (transform) {
            return transform->position;
        }
    }
    
    // 尝试查找任何在原点附近的实体（假设是太阳）
    auto view = registry.view<TransformComponent>();
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        float dist = std::sqrt(
            transform.position.x * transform.position.x +
            transform.position.y * transform.position.y +
            transform.position.z * transform.position.z
        );
        // 如果实体接近原点，假设它是太阳
        if (dist < 50.0f) {
            return transform.position;
        }
    }
    
    return defaultSunPos;
}

components::CameraComponent* RenderSystem::FindActiveCamera(entt::registry& registry) {
    components::CameraComponent* activeCamera = nullptr;

    // Find any entity with an active CameraComponent
    // 注意：不再用 TransformComponent.position 覆盖 camera.position
    // 玩家相机由 PlayerSystem 直接设置 camera.position/target
    // 自由相机由 FreeCameraSystem 设置
    auto view = registry.view<components::CameraComponent>();
    view.each([&](auto entity, auto& cam) {
        if (cam.isActive) {
            activeCamera = &cam;
        }
    });

    return activeCamera;
}

} // namespace outer_wilds
