#include "RenderQueue.h"
#include "components/MeshComponent.h"
#include "../scene/components/TransformComponent.h"
#include "../core/DebugManager.h"
#include "resources/Material.h"
#include "resources/Mesh.h"
#include "resources/Shader.h"
#include <unordered_map>
#include <algorithm>

using namespace DirectX;

namespace outer_wilds {

/**
 * @brief 全局Shader缓存（避免重复加载）
 */
static std::unordered_map<std::string, std::shared_ptr<resources::Shader>> g_ShaderCache;
static ID3D11Device* g_CachedDevice = nullptr;

static resources::Shader* GetOrLoadShader(const std::string& vsName, const std::string& psName, ID3D11Device* device) {
    std::string key = vsName + "|" + psName;
    
    if (g_ShaderCache.find(key) == g_ShaderCache.end()) {
        auto shader = std::make_shared<resources::Shader>();
        if (shader->LoadFromFile(device, vsName, psName)) {
            g_ShaderCache[key] = shader;
            DebugManager::GetInstance().Log("RenderQueue", "Cached shader: " + key);
        } else {
            DebugManager::GetInstance().Log("RenderQueue", "Failed to load shader: " + key);
            return nullptr;
        }
    }
    
    return g_ShaderCache[key].get();
}

/**
 * @brief 从ECS收集渲染批次
 */
void RenderQueue::CollectFromECS(entt::registry& registry, const XMFLOAT3& cameraPos) {
    Clear();
    
    // ID映射表（用于计算sortKey）
    std::unordered_map<ID3D11VertexShader*, uint8_t> shaderIDs;
    std::unordered_map<ID3D11ShaderResourceView*, uint8_t> materialIDs;
    uint8_t nextShaderID = 0;
    uint8_t nextMaterialID = 0;
    
    XMVECTOR camPos = XMLoadFloat3(&cameraPos);
    
    auto view = registry.view<components::MeshComponent, TransformComponent>();
    for (auto entity : view) {
        auto& meshComp = view.get<components::MeshComponent>(entity);
        auto& transformComp = view.get<TransformComponent>(entity);
        
        // 跳过无效/不可见Mesh
        if (!meshComp.mesh || !meshComp.mesh->vertexBuffer || !meshComp.isVisible) {
            continue;
        }
        
        // 构建RenderBatch
        RenderBatch batch;
        
        // === GPU资源 ===
        batch.vertexBuffer = static_cast<ID3D11Buffer*>(meshComp.mesh->vertexBuffer);
        batch.indexBuffer = static_cast<ID3D11Buffer*>(meshComp.mesh->indexBuffer);
        batch.indexCount = meshComp.mesh->GetIndexCount();
        batch.vertexStride = sizeof(resources::Vertex);
        batch.vertexOffset = 0;
        
        // 获取D3D设备（用于按需加载shader）
        if (!g_CachedDevice && batch.vertexBuffer) {
            batch.vertexBuffer->GetDevice(&g_CachedDevice);
        }
        
        // === Multi-Texture PBR Resource Extraction ===
        resources::Shader* shaderToUse = nullptr;
        ID3D11ShaderResourceView* albedoSRV = nullptr;
        ID3D11ShaderResourceView* normalSRV = nullptr;
        ID3D11ShaderResourceView* metallicSRV = nullptr;
        ID3D11ShaderResourceView* roughnessSRV = nullptr;
        
        if (meshComp.material) {
            // Extract all PBR textures from material
            albedoSRV = static_cast<ID3D11ShaderResourceView*>(meshComp.material->albedoTextureSRV);
            normalSRV = static_cast<ID3D11ShaderResourceView*>(meshComp.material->normalTextureSRV);
            metallicSRV = static_cast<ID3D11ShaderResourceView*>(meshComp.material->metallicTextureSRV);
            roughnessSRV = static_cast<ID3D11ShaderResourceView*>(meshComp.material->roughnessTextureSRV);
            
            // Backward compatibility: fallback to old shaderProgram field
            if (!albedoSRV && meshComp.material->shaderProgram) {
                void* ptr = meshComp.material->shaderProgram;
                ID3D11ShaderResourceView* testSRV = static_cast<ID3D11ShaderResourceView*>(ptr);
                IUnknown* testUnknown = nullptr;
                
                HRESULT hr = testSRV->QueryInterface(__uuidof(ID3D11ShaderResourceView), (void**)&testUnknown);
                if (SUCCEEDED(hr) && testUnknown) {
                    testUnknown->Release();
                    albedoSRV = testSRV;  // Old single-texture path
                }
            }
            
            // Select appropriate shader based on available textures
            if (g_CachedDevice) {
                if (albedoSRV || normalSRV || metallicSRV || roughnessSRV) {
                    shaderToUse = GetOrLoadShader("textured.vs", "textured.ps", g_CachedDevice);
                } else {
                    shaderToUse = GetOrLoadShader("basic.vs", "basic.ps", g_CachedDevice);
                }
            }
        }
        
        // Fallback to basic shader if no material
        if (!shaderToUse && g_CachedDevice) {
            shaderToUse = GetOrLoadShader("basic.vs", "basic.ps", g_CachedDevice);
        }
        
        // 绑定Shader和多纹理到batch
        if (shaderToUse) {
            batch.vertexShader = shaderToUse->GetVertexShader();
            batch.pixelShader = shaderToUse->GetPixelShader();
            batch.inputLayout = shaderToUse->GetInputLayout();
            batch.albedoTexture = albedoSRV;
            batch.normalTexture = normalSRV;
            batch.metallicTexture = metallicSRV;
            batch.roughnessTexture = roughnessSRV;
            batch.material = meshComp.material.get();
            
            // 分配Shader ID
            if (batch.vertexShader && shaderIDs.find(batch.vertexShader) == shaderIDs.end()) {
                shaderIDs[batch.vertexShader] = nextShaderID++;
            }
            
            // 分配Material ID（基于albedo纹理）
            if (batch.albedoTexture && materialIDs.find(batch.albedoTexture) == materialIDs.end()) {
                materialIDs[batch.albedoTexture] = nextMaterialID++;
            }
        } else {
            // 没有有效shader，跳过此entity
            continue;
        }
        
        // === 变换矩阵 ===
        XMMATRIX translation = XMMatrixTranslation(
            transformComp.position.x,
            transformComp.position.y,
            transformComp.position.z
        );
        XMMATRIX rotation = XMMatrixRotationQuaternion(XMLoadFloat4(&transformComp.rotation));
        XMMATRIX scale = XMMatrixScaling(
            transformComp.scale.x,
            transformComp.scale.y,
            transformComp.scale.z
        );
        batch.worldMatrix = scale * rotation * translation;
        
        // === 计算深度 ===
        XMVECTOR objPos = XMLoadFloat3(&transformComp.position);
        XMVECTOR delta = XMVectorSubtract(objPos, camPos);
        float distanceSquared = XMVectorGetX(XMVector3LengthSq(delta));
        uint16_t depth = static_cast<uint16_t>((std::min)(distanceSquared, 65535.0f));
        
        // === 计算排序键 ===
        batch.renderPass = (meshComp.material && meshComp.material->isTransparent) ? 1 : 0;
        uint8_t shaderID = batch.vertexShader ? shaderIDs[batch.vertexShader] : 0;
        uint8_t matID = batch.albedoTexture ? materialIDs[batch.albedoTexture] : 0;
        batch.CalculateSortKey(shaderID, matID, depth);
        
        m_Batches.push_back(batch);
    }
    
    m_Stats.totalBatches = static_cast<uint32_t>(m_Batches.size());
    
    // === 调试（已禁用）===
    // static int collectCount = 0;
    // if (collectCount++ < 5) {
    //     DebugManager::GetInstance().Log("RenderQueue::Collect", ...);
    // }
}

/**
 * @brief 执行绘制（带状态缓存）
 */
void RenderQueue::Execute(ID3D11DeviceContext* context, ID3D11Buffer* perObjectCB) {
    if (!context || m_Batches.empty()) {
        return;
    }
    
    m_Stats.Reset();
    m_Stats.totalBatches = static_cast<uint32_t>(m_Batches.size());
    
    // === 创建默认Sampler（静态，只创建一次）===
    static ID3D11SamplerState* s_defaultSampler = nullptr;
    if (!s_defaultSampler && g_CachedDevice) {
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.MaxAnisotropy = 1;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        samplerDesc.MinLOD = 0;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        g_CachedDevice->CreateSamplerState(&samplerDesc, &s_defaultSampler);
    }
    
    // === 状态缓存 ===
    ID3D11VertexShader* lastVS = nullptr;
    ID3D11PixelShader* lastPS = nullptr;
    ID3D11InputLayout* lastLayout = nullptr;
    ID3D11ShaderResourceView* lastAlbedo = nullptr;
    ID3D11ShaderResourceView* lastNormal = nullptr;
    ID3D11ShaderResourceView* lastMetallic = nullptr;
    ID3D11ShaderResourceView* lastRoughness = nullptr;
    bool samplerBound = false;
    
    int batchIndex = 0;
    for (const auto& batch : m_Batches) {
        
        // === 绑定Shader（仅在切换时） ===
        if (batch.vertexShader != lastVS) {
            context->VSSetShader(batch.vertexShader, nullptr, 0);
            lastVS = batch.vertexShader;
            m_Stats.shaderSwitches++;
        }
        
        if (batch.pixelShader != lastPS) {
            context->PSSetShader(batch.pixelShader, nullptr, 0);
            lastPS = batch.pixelShader;
        }
        
        if (batch.inputLayout != lastLayout) {
            context->IASetInputLayout(batch.inputLayout);
            lastLayout = batch.inputLayout;
        }
        
        // === 绑定多纹理（PBR工作流：仅在切换时绑定）===
        // Texture slot 0: Albedo/Diffuse map
        if (batch.albedoTexture != lastAlbedo) {
            if (batch.albedoTexture) {
                context->PSSetShaderResources(0, 1, &batch.albedoTexture);
                m_Stats.textureSwitches++;
                
                // 绑定sampler（只需一次）
                if (!samplerBound && s_defaultSampler) {
                    context->PSSetSamplers(0, 1, &s_defaultSampler);
                    samplerBound = true;
                }
            } else {
                ID3D11ShaderResourceView* nullSRV = nullptr;
                context->PSSetShaderResources(0, 1, &nullSRV);
            }
            lastAlbedo = batch.albedoTexture;
        }
        
        // Texture slot 1: Normal map
        if (batch.normalTexture != lastNormal) {
            if (batch.normalTexture) {
                context->PSSetShaderResources(1, 1, &batch.normalTexture);
            } else {
                ID3D11ShaderResourceView* nullSRV = nullptr;
                context->PSSetShaderResources(1, 1, &nullSRV);
            }
            lastNormal = batch.normalTexture;
        }
        
        // Texture slot 2: Metallic map
        if (batch.metallicTexture != lastMetallic) {
            if (batch.metallicTexture) {
                context->PSSetShaderResources(2, 1, &batch.metallicTexture);
            } else {
                ID3D11ShaderResourceView* nullSRV = nullptr;
                context->PSSetShaderResources(2, 1, &nullSRV);
            }
            lastMetallic = batch.metallicTexture;
        }
        
        // Texture slot 3: Roughness map
        if (batch.roughnessTexture != lastRoughness) {
            if (batch.roughnessTexture) {
                context->PSSetShaderResources(3, 1, &batch.roughnessTexture);
            } else {
                ID3D11ShaderResourceView* nullSRV = nullptr;
                context->PSSetShaderResources(3, 1, &nullSRV);
            }
            lastRoughness = batch.roughnessTexture;
        }
        
        // === 更新PerObject常量缓冲区 ===
        if (perObjectCB) {
            struct PerObjectData {
                XMMATRIX world;
                XMFLOAT4 color;
            };
            
            PerObjectData objData;
            objData.world = XMMatrixTranspose(batch.worldMatrix);
            // 从material读取albedo颜色,如果没有则使用白色
            objData.color = batch.material ? batch.material->albedo : XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            
            D3D11_MAPPED_SUBRESOURCE mappedResource;
            if (SUCCEEDED(context->Map(perObjectCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
                memcpy(mappedResource.pData, &objData, sizeof(PerObjectData));
                context->Unmap(perObjectCB, 0);
            }
            
            // 绑定到slot 1 (shader中的register(b1))
            context->VSSetConstantBuffers(1, 1, &perObjectCB);
        }
        
        // === 绑定顶点/索引缓冲区 ===
        UINT stride = batch.vertexStride;
        UINT offset = batch.vertexOffset;
        context->IASetVertexBuffers(0, 1, &batch.vertexBuffer, &stride, &offset);
        context->IASetIndexBuffer(batch.indexBuffer, DXGI_FORMAT_R32_UINT, 0);
        
        // === 设置图元拓扑 ===
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        
        // === DrawCall ===
        context->DrawIndexed(batch.indexCount, 0, 0);
        m_Stats.drawCalls++;
        
        batchIndex++;
    }
}

} // namespace outer_wilds
