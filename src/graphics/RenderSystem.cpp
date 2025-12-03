#include "RenderSystem.h"
#include "../scene/Scene.h"
#include "../scene/SceneManager.h"
#include "components/CameraComponent.h"
#include "components/RenderableComponent.h"
#include "components/MeshComponent.h"
#include "components/RenderPriorityComponent.h"
#include "../scene/components/TransformComponent.h"
#include "../core/DebugManager.h"
#include <DirectXMath.h>
#include <d3d11.h>
#include <vector>

namespace outer_wilds {

void RenderSystem::Initialize(SceneManager* sceneManager) {
    m_SceneManager = sceneManager;
    m_Backend = std::make_unique<RenderBackend>();
}

void RenderSystem::Update(float deltaTime, entt::registry& registry) {
    m_UpdateCounter++;
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

    // Render all entities with RenderableComponent
    RenderScene(camera, scenePtr->GetRegistry(), shouldDebug);

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
    static ID3D11Buffer* s_perObjectCB = nullptr;

    if (!s_perFrameCB) {
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.ByteWidth = sizeof(DirectX::XMMATRIX);
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(&cbDesc, nullptr, &s_perFrameCB);
    }
    if (!s_perObjectCB) {
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.ByteWidth = sizeof(DirectX::XMMATRIX);
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(&cbDesc, nullptr, &s_perObjectCB);
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
    // 阶段1：更新渲染优先级（距离、LOD等）
    // ============================================
    UpdateRenderPriorities(registry, camera->position);

    // ============================================
    // 阶段2：收集实体到队列
    // ============================================
    m_OpaqueQueue.Clear();
    m_TransparentQueue.Clear();
    
    // Collect entities with RenderableComponent (legacy ground plane)
    auto renderables = registry.view<
        const components::RenderableComponent, 
        const TransformComponent
    >();
    
    for (auto entity : renderables) {
        // 检查是否有 RenderPriority 组件
        auto* priority = registry.try_get<components::RenderPriorityComponent>(entity);
        
        if (priority) {
            // 跳过不可见的实体（视锥剔除）
            if (!priority->visible) continue;
            
            // 按 renderPass 分类到不同队列
            if (priority->renderPass == 0) {  // Opaque
                m_OpaqueQueue.Push(entity);
            } else if (priority->renderPass == 1) {  // Transparent
                m_TransparentQueue.Push(entity);
            }
        } else {
            // 没有 Priority 组件的实体默认为 Opaque
            m_OpaqueQueue.Push(entity);
        }
    }
    
    // Collect entities with MeshComponent (new model loading system)
    auto meshEntities = registry.view<
        const components::MeshComponent,
        const TransformComponent
    >();
    
    for (auto entity : meshEntities) {
        auto* priority = registry.try_get<components::RenderPriorityComponent>(entity);
        
        if (priority) {
            if (!priority->visible) continue;
            
            if (priority->renderPass == 0) {
                m_OpaqueQueue.Push(entity);
            } else if (priority->renderPass == 1) {
                m_TransparentQueue.Push(entity);
            }
        } else {
            // Default to opaque
            m_OpaqueQueue.Push(entity);
        }
    }
    
    // ============================================
    // 阶段3：排序队列
    // ============================================
    m_OpaqueQueue.Sort(registry, true);   // 前到后排序
    m_TransparentQueue.Sort(registry, false);  // 后到前排序
    
    if (shouldDebug) {
        DebugManager::GetInstance().Log("RenderSystem", 
            "Queue Stats - Opaque: " + std::to_string(m_OpaqueQueue.Size()) +
            ", Transparent: " + std::to_string(m_TransparentQueue.Size()));
    }

    // ============================================
    // 阶段4：渲染队列
    // ============================================
    // 先渲染不透明物体
    DrawQueue(m_OpaqueQueue, registry);
    
    // 再渲染透明物体（未来用于水、大气等）
    if (!m_TransparentQueue.Empty()) {
        // TODO: 启用Alpha混合状态
        DrawQueue(m_TransparentQueue, registry);
        // TODO: 恢复渲染状态
    }
    
    // 每60帧输出一次详细的渲染顺序信息
    static int frameCount = 0;
    if (++frameCount >= 60) {
        frameCount = 0;
        outer_wilds::DebugManager::GetInstance().Log("RenderSystem", "=== Render Order Debug ===");
        outer_wilds::DebugManager::GetInstance().Log("RenderSystem", "Opaque Queue:");
        int idx = 0;
        for (const auto& entity : m_OpaqueQueue.GetEntities()) {
            const auto* priority = registry.try_get<components::RenderPriorityComponent>(entity);
            if (priority) {
                std::string msg = "  [" + std::to_string(idx++) + "] Entity ID=" + 
                                std::to_string(static_cast<uint32_t>(entity)) + 
                                ", sortKey=" + std::to_string(priority->sortKey) +
                                ", distance=" + std::to_string(priority->distanceToCamera);
                outer_wilds::DebugManager::GetInstance().Log("RenderSystem", msg);
            }
        }
        outer_wilds::DebugManager::GetInstance().Log("RenderSystem", "Transparent Queue:");
        idx = 0;
        for (const auto& entity : m_TransparentQueue.GetEntities()) {
            const auto* priority = registry.try_get<components::RenderPriorityComponent>(entity);
            if (priority) {
                std::string msg = "  [" + std::to_string(idx++) + "] Entity ID=" + 
                                std::to_string(static_cast<uint32_t>(entity)) + 
                                ", sortKey=" + std::to_string(priority->sortKey) +
                                ", distance=" + std::to_string(priority->distanceToCamera);
                outer_wilds::DebugManager::GetInstance().Log("RenderSystem", msg);
            }
        }
        outer_wilds::DebugManager::GetInstance().Log("RenderSystem", "========================");
    }
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

// ============================================
// 更新所有实体的渲染优先级
// ============================================
void RenderSystem::UpdateRenderPriorities(entt::registry& registry, const DirectX::XMFLOAT3& cameraPos) {
    auto view = registry.view<const TransformComponent, components::RenderPriorityComponent>();
    
    DirectX::XMVECTOR camPosVec = DirectX::XMLoadFloat3(&cameraPos);
    
    for (auto [entity, transform, priority] : view.each()) {
        // 计算到相机的距离
        DirectX::XMVECTOR entityPos = DirectX::XMLoadFloat3(&transform.position);
        DirectX::XMVECTOR diff = DirectX::XMVectorSubtract(entityPos, camPosVec);
        DirectX::XMVECTOR lengthVec = DirectX::XMVector3Length(diff);
        
        float distance;
        DirectX::XMStoreFloat(&distance, lengthVec);
        priority.distanceToCamera = distance;
        
        // TODO: 未来可以在这里实现 LOD 更新
        // if (distance < 50.0f) priority.lodLevel = 0;
        // else if (distance < 150.0f) priority.lodLevel = 1;
        // else priority.lodLevel = 2;
    }
}

// ============================================
// 绘制队列中的所有实体
// ============================================
void RenderSystem::DrawQueue(const LightweightRenderQueue& queue, entt::registry& registry) {
    if (queue.Empty()) return;
    
    auto device = static_cast<ID3D11Device*>(m_Backend->GetDevice());
    auto context = static_cast<ID3D11DeviceContext*>(m_Backend->GetContext());
    
    static bool s_firstRun = true;
    
    // 获取静态的 perObjectCB
    static ID3D11Buffer* s_perObjectCB = nullptr;
    if (!s_perObjectCB) {
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.ByteWidth = sizeof(DirectX::XMMATRIX);
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(&cbDesc, nullptr, &s_perObjectCB);
        if (s_firstRun) DebugManager::GetInstance().Log("DrawQueue", "Created per-object constant buffer");
    }
    
    // 遍历队列中的所有实体
    int drawCount = 0;
    for (entt::entity entity : queue.GetEntities()) {
        auto& transform = registry.get<TransformComponent>(entity);
        
        // Check for RenderableComponent (legacy)
        auto* renderable = registry.try_get<components::RenderableComponent>(entity);
        if (renderable) {
            // 验证组件
            if (!renderable->mesh || !renderable->shader) {
                if (s_firstRun) DebugManager::GetInstance().Log("DrawQueue", "Skipping entity: missing mesh or shader");
                continue;
            }
            
            // 创建 GPU buffers
            if (!renderable->mesh->vertexBuffer) {
                const_cast<resources::Mesh*>(renderable->mesh.get())->CreateGPUBuffers(device);
            }
            if (!renderable->mesh->vertexBuffer) {
                if (s_firstRun) DebugManager::GetInstance().Log("DrawQueue", "Skipping entity: failed to create vertex buffer");
                continue;
            }
            
            // 获取世界矩阵
            DirectX::XMMATRIX worldMatrix = transform.GetWorldMatrix();
            
            // 绑定 shader
            renderable->shader->Bind(context);
            
            // 更新 per-object constant buffer
            if (s_perObjectCB) {
                D3D11_MAPPED_SUBRESOURCE mapped;
                if (SUCCEEDED(context->Map(s_perObjectCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                    DirectX::XMMATRIX worldTransposed = DirectX::XMMatrixTranspose(worldMatrix);
                    memcpy(mapped.pData, &worldTransposed, sizeof(DirectX::XMMATRIX));
                    context->Unmap(s_perObjectCB, 0);
                }
                context->VSSetConstantBuffers(1, 1, &s_perObjectCB);
            }
            
            // 设置顶点缓冲
            UINT stride = sizeof(resources::Vertex);
            UINT offset = 0;
            auto vertexBuffer = static_cast<ID3D11Buffer*>(renderable->mesh->vertexBuffer);
            context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
            
            // 设置索引缓冲
            if (renderable->mesh->indexBuffer) {
                auto indexBuffer = static_cast<ID3D11Buffer*>(renderable->mesh->indexBuffer);
                context->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
            }
            
            // 设置拓扑结构
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            
            // 绘制
            if (renderable->mesh->indexBuffer && renderable->mesh->GetIndexCount() > 0) {
                context->DrawIndexed(static_cast<UINT>(renderable->mesh->GetIndexCount()), 0, 0);
                drawCount++;
            } else {
                context->Draw(static_cast<UINT>(renderable->mesh->GetVertices().size()), 0);
                drawCount++;
            }
            continue;
        }
        
        // Check for MeshComponent (new system)
        auto* meshComp = registry.try_get<components::MeshComponent>(entity);
        if (meshComp && meshComp->mesh && meshComp->isVisible) {
            // Create GPU buffers if needed
            if (!meshComp->mesh->vertexBuffer) {
                const_cast<resources::Mesh*>(meshComp->mesh.get())->CreateGPUBuffers(device);
            }
            if (!meshComp->mesh->vertexBuffer) {
                if (s_firstRun) DebugManager::GetInstance().Log("DrawQueue", "Failed to create vertex buffer for mesh entity");
                continue;
            }
            
            // Get world matrix
            DirectX::XMMATRIX worldMatrix = transform.GetWorldMatrix();
            
            // TODO: Bind proper shader for textured mesh
            // For now, we'll use a simple default shader
            // In production, load shader from material or use a default textured shader
            
            // Update per-object constant buffer
            if (s_perObjectCB) {
                D3D11_MAPPED_SUBRESOURCE mapped;
                if (SUCCEEDED(context->Map(s_perObjectCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                    DirectX::XMMATRIX worldTransposed = DirectX::XMMatrixTranspose(worldMatrix);
                    memcpy(mapped.pData, &worldTransposed, sizeof(DirectX::XMMATRIX));
                    context->Unmap(s_perObjectCB, 0);
                }
                context->VSSetConstantBuffers(1, 1, &s_perObjectCB);
            }
            
            // Set vertex buffer
            UINT stride = sizeof(resources::Vertex);
            UINT offset = 0;
            auto vertexBuffer = static_cast<ID3D11Buffer*>(meshComp->mesh->vertexBuffer);
            context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
            
            // Set index buffer
            if (meshComp->mesh->indexBuffer) {
                auto indexBuffer = static_cast<ID3D11Buffer*>(meshComp->mesh->indexBuffer);
                context->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
            }
            
            // Set topology
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            
            // Bind texture if available
            if (meshComp->material && meshComp->material->shaderProgram) {
                auto textureSRV = static_cast<ID3D11ShaderResourceView*>(meshComp->material->shaderProgram);
                context->PSSetShaderResources(0, 1, &textureSRV);
                
                // Set sampler state
                static ID3D11SamplerState* s_samplerState = nullptr;
                if (!s_samplerState) {
                    D3D11_SAMPLER_DESC samplerDesc = {};
                    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
                    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
                    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
                    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
                    samplerDesc.MaxAnisotropy = 1;
                    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
                    samplerDesc.MinLOD = 0;
                    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
                    device->CreateSamplerState(&samplerDesc, &s_samplerState);
                }
                if (s_samplerState) {
                    context->PSSetSamplers(0, 1, &s_samplerState);
                }
            }
            
            // Draw
            if (meshComp->mesh->indexBuffer && meshComp->mesh->GetIndexCount() > 0) {
                context->DrawIndexed(static_cast<UINT>(meshComp->mesh->GetIndexCount()), 0, 0);
                drawCount++;
            } else {
                context->Draw(static_cast<UINT>(meshComp->mesh->GetVertices().size()), 0);
                drawCount++;
            }
        }
    }
    
    if (s_firstRun) {
        DebugManager::GetInstance().Log("DrawQueue", "Drew " + std::to_string(drawCount) + " entities from queue of " + std::to_string(queue.Size()));
        s_firstRun = false;
    }
}

} // namespace outer_wilds
