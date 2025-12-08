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
    static int collectDebugCount = 0;
    if (collectDebugCount++ < 3) {
        std::cout << "[RenderQueue::CollectFromECS] IMMEDIATE: Called #" << collectDebugCount << std::endl;
    }
    
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
        
        // === Shader + 纹理资源解析（兼容多种情况）===
        resources::Shader* shaderToUse = nullptr;
        ID3D11ShaderResourceView* textureSRV = nullptr;
        
        if (meshComp.material && meshComp.material->shaderProgram) {
            void* ptr = meshComp.material->shaderProgram;
            
            // 方法1: 尝试作为SRV进行COM查询(更安全)
            ID3D11ShaderResourceView* testSRV = static_cast<ID3D11ShaderResourceView*>(ptr);
            IUnknown* testUnknown = nullptr;
            
            // 检查是否是有效的D3D11接口
            HRESULT hr = testSRV->QueryInterface(__uuidof(ID3D11ShaderResourceView), (void**)&testUnknown);
            
            if (SUCCEEDED(hr) && testUnknown) {
                // 情况2：material->shaderProgram 是 textureSRV （SceneAssetLoader的临时hack）
                testUnknown->Release();  // 释放QueryInterface增加的引用计数
                
                textureSRV = testSRV;
                
                // 使用默认textured shader
                if (g_CachedDevice) {
                    shaderToUse = GetOrLoadShader("textured.vs", "textured.ps", g_CachedDevice);
                }
            } else {
                // 情况1：material->shaderProgram 是 Shader* （正确方式）
                shaderToUse = static_cast<resources::Shader*>(ptr);
                // TODO: 未来从material.texture字段获取纹理
            }
        }
        
        // 如果没有material或没有shader，使用默认basic shader（无纹理）
        if (!shaderToUse && g_CachedDevice) {
            shaderToUse = GetOrLoadShader("basic.vs", "basic.ps", g_CachedDevice);
            textureSRV = nullptr;  // 确保使用basic shader时不使用纹理
        }
        
        // === 调试：输出shader解析结果 ===
        static int debugCount = 0;
        if (debugCount++ < 5) {  // 输出前5个entity
            std::cout << "[RenderQueue::Collect] Entity #" << debugCount 
                      << ": shader=" << (shaderToUse ? "OK" : "NULL")
                      << ", texture=" << (textureSRV ? "OK" : "NULL")
                      << ", VS=" << (shaderToUse && shaderToUse->GetVertexShader() ? "OK" : "NULL")
                      << ", PS=" << (shaderToUse && shaderToUse->GetPixelShader() ? "OK" : "NULL")
                      << ", InputLayout=" << (shaderToUse && shaderToUse->GetInputLayout() ? "OK" : "NULL")
                      << std::endl;
        }
        
        // 绑定Shader到batch
        if (shaderToUse) {
            batch.vertexShader = shaderToUse->GetVertexShader();
            batch.pixelShader = shaderToUse->GetPixelShader();
            batch.inputLayout = shaderToUse->GetInputLayout();
            batch.texture = textureSRV;
            batch.material = meshComp.material.get();  // 保存material指针用于读取albedo
            
            // 分配Shader ID
            if (batch.vertexShader && shaderIDs.find(batch.vertexShader) == shaderIDs.end()) {
                shaderIDs[batch.vertexShader] = nextShaderID++;
            }
            
            // 分配Material ID（基于纹理）
            if (batch.texture && materialIDs.find(batch.texture) == materialIDs.end()) {
                materialIDs[batch.texture] = nextMaterialID++;
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
        uint8_t matID = batch.texture ? materialIDs[batch.texture] : 0;
        batch.CalculateSortKey(shaderID, matID, depth);
        
        m_Batches.push_back(batch);
    }
    
    m_Stats.totalBatches = static_cast<uint32_t>(m_Batches.size());
    
    // === 调试：立即输出收集结果 ===
    static int collectCount = 0;
    if (collectCount++ < 5) {  // 只输出前5次
        DebugManager::GetInstance().Log("RenderQueue::Collect", 
            "Collected " + std::to_string(m_Batches.size()) + " batches");
    }
}

/**
 * @brief 执行绘制（带状态缓存）
 */
void RenderQueue::Execute(ID3D11DeviceContext* context, ID3D11Buffer* perObjectCB) {
    static int executeDebugCount = 0;
    if (executeDebugCount++ < 3) {
        std::cout << "[RenderQueue::Execute] IMMEDIATE: Called #" << executeDebugCount 
                  << ", batches=" << m_Batches.size() << std::endl;
    }
    
    if (!context || m_Batches.empty()) {
        if (executeDebugCount <= 3) {
            std::cout << "[RenderQueue::Execute] Early return: context=" << (context ? "OK" : "NULL") 
                      << ", batches=" << m_Batches.size() << std::endl;
        }
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
    ID3D11ShaderResourceView* lastTexture = nullptr;
    bool samplerBound = false;
    
    if (executeDebugCount <= 3) {
        std::cout << "[RenderQueue::Execute] About to iterate " << m_Batches.size() << " batches" << std::endl;
    }
    
    int batchIndex = 0;
    for (const auto& batch : m_Batches) {
        if (executeDebugCount <= 3) {
            std::cout << "[RenderQueue::Execute] Batch " << batchIndex 
                      << ": VS=" << (batch.vertexShader ? "OK" : "NULL")
                      << ", PS=" << (batch.pixelShader ? "OK" : "NULL")
                      << ", InputLayout=" << (batch.inputLayout ? "OK" : "NULL")
                      << ", VB=" << (batch.vertexBuffer ? "OK" : "NULL")
                      << ", IB=" << (batch.indexBuffer ? "OK" : "NULL")
                      << ", texture=" << (batch.texture ? "OK" : "NULL")
                      << ", indexCount=" << batch.indexCount << std::endl;
        }
        
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
        
        // === 绑定纹理（仅在切换时） ===
        if (batch.texture != lastTexture) {
            if (batch.texture) {
                context->PSSetShaderResources(0, 1, &batch.texture);
                m_Stats.textureSwitches++;
                
                // 绑定sampler（只需一次）
                if (!samplerBound && s_defaultSampler) {
                    context->PSSetSamplers(0, 1, &s_defaultSampler);
                    samplerBound = true;
                }
            } else {
                // 显式清空纹理槽（修复纹理共享Bug）
                ID3D11ShaderResourceView* nullSRV = nullptr;
                context->PSSetShaderResources(0, 1, &nullSRV);
                m_Stats.textureSwitches++;
            }
            lastTexture = batch.texture;
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
        
        if (executeDebugCount <= 3) {
            std::cout << "[RenderQueue::Execute] Batch " << batchIndex << " drawn successfully" << std::endl;
        }
        batchIndex++;
        
        // === 调试：输出第一个batch的详细信息 ===
        static int batchDebugCount = 0;
        if (batchDebugCount++ == 0) {
            DebugManager::GetInstance().Log("RenderQueue::Execute",
                "First batch: indexCount=" + std::to_string(batch.indexCount) +
                ", vertexStride=" + std::to_string(batch.vertexStride) +
                ", VS=" + std::string(batch.vertexShader ? "OK" : "NULL") +
                ", PS=" + std::string(batch.pixelShader ? "OK" : "NULL") +
                ", InputLayout=" + std::string(batch.inputLayout ? "OK" : "NULL"));
        }
    }
    
    // === 调试日志（前5帧输出详细信息）===
    static int frameCount = 0;
    if (frameCount++ < 5) {
        DebugManager::GetInstance().Log("RenderQueue::Execute",
            "Frame " + std::to_string(frameCount) +
            ": Batches=" + std::to_string(m_Stats.totalBatches) +
            ", Draws=" + std::to_string(m_Stats.drawCalls) +
            ", ShaderSwitches=" + std::to_string(m_Stats.shaderSwitches) +
            ", TextureSwitches=" + std::to_string(m_Stats.textureSwitches));
    }
    
    // 每100帧输出统计
    if (frameCount >= 100 && frameCount % 100 == 0) {
        DebugManager::GetInstance().Log("RenderQueue",
            "Stats: " + std::to_string(m_Stats.totalBatches) + " batches, " +
            std::to_string(m_Stats.drawCalls) + " draws");
    }
}

} // namespace outer_wilds
