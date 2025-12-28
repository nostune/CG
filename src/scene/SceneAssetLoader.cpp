/**
 * SceneAssetLoader.cpp
 * 
 * 场景资源加载器 - 简化版（不包含物理设置）
 * 
 * 【重要】所有物理碰撞体必须在调用方手动创建
 * 本 loader 只负责加载模型和创建渲染组件
 */

#include "SceneAssetLoader.h"
#include "Scene.h"
#include "components/TransformComponent.h"
#include "../graphics/components/MeshComponent.h"
#include "../graphics/components/RenderableComponent.h"
#include "../graphics/components/RenderPriorityComponent.h"
#include "../graphics/resources/OBJLoader.h"
#include "../graphics/resources/AssimpLoader.h"
#include "../graphics/resources/TextureLoader.h"
#include "../core/DebugManager.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace outer_wilds {

using namespace components;
using namespace resources;

// 辅助函数：获取文件扩展名（小写）
static std::string GetFileExtension(const std::string& filePath) {
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos == std::string::npos) return "";
    std::string ext = filePath.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

entt::entity SceneAssetLoader::LoadModelAsEntity(
    entt::registry& registry,
    std::shared_ptr<Scene> scene,
    ID3D11Device* device,
    const std::string& objPath,
    const std::string& texturePath,
    const DirectX::XMFLOAT3& position,
    const DirectX::XMFLOAT3& scale,
    const PhysicsOptions* physicsOpts  // 【忽略】物理选项不再使用
) {
    std::string ext = GetFileExtension(objPath);
    std::shared_ptr<Mesh> mesh = nullptr;
    std::vector<std::string> fbxTexturePaths;
    std::vector<EmbeddedTexture> embeddedTextures;  // For GLB embedded textures
    
    // ============================================
    // 根据文件格式加载 Mesh
    // ============================================
    if (ext == "fbx" || ext == "gltf" || ext == "glb" || ext == "dae" || ext == "blend" || ext == "3ds") {
        // Use AssimpLoader for FBX, GLTF, GLB and other formats
        LoadedModel model;
        if (!AssimpLoader::LoadFromFile(objPath, model)) {
            DebugManager::GetInstance().Log("SceneAssetLoader", "Assimp loading failed: " + objPath);
            return entt::null;
        }
        mesh = model.mesh;
        fbxTexturePaths = model.texturePaths;
        embeddedTextures = model.embeddedTextures;  // Store embedded textures
        
        DebugManager::GetInstance().Log("SceneAssetLoader", 
            "Loaded mesh (Assimp) with " + std::to_string(mesh->GetVertices().size()) + " vertices" +
            ", embedded textures: " + std::to_string(embeddedTextures.size()));
    } else {
        // Use OBJLoader for OBJ files
        mesh = std::make_shared<Mesh>();
        if (!OBJLoader::LoadFromFile(objPath, *mesh)) {
            DebugManager::GetInstance().Log("SceneAssetLoader", "OBJ loading failed: " + objPath);
            return entt::null;
        }
        DebugManager::GetInstance().Log("SceneAssetLoader", 
            "Loaded mesh (OBJ) with " + std::to_string(mesh->GetVertices().size()) + " vertices");
    }
    
    if (!mesh) {
        DebugManager::GetInstance().Log("SceneAssetLoader", "Failed to load mesh: " + objPath);
        return entt::null;
    }

    // Create mesh GPU buffers
    mesh->CreateGPUBuffers(device);

    // ============================================
    // 材质和纹理处理 - 支持GLB嵌入式纹理
    // ============================================
    std::shared_ptr<Material> material = nullptr;
    
    // Check for embedded textures first (GLB format)
    // 检查是否有任何嵌入式纹理（albedo 或 emissive）
    bool hasEmbeddedAlbedo = !embeddedTextures.empty() && 
                             embeddedTextures.size() > LoadedModel::ALBEDO &&
                             !embeddedTextures[LoadedModel::ALBEDO].data.empty();
    bool hasEmbeddedEmissive = !embeddedTextures.empty() && 
                               embeddedTextures.size() > LoadedModel::EMISSIVE &&
                               !embeddedTextures[LoadedModel::EMISSIVE].data.empty();
    
    if (hasEmbeddedAlbedo || hasEmbeddedEmissive) {
        // Create material from embedded textures
        material = CreateMaterialFromEmbedded(device, embeddedTextures);
        if (material) {
            DebugManager::GetInstance().Log("SceneAssetLoader", 
                "Created material from embedded textures (albedo=" + 
                std::string(hasEmbeddedAlbedo ? "yes" : "no") + 
                ", emissive=" + std::string(hasEmbeddedEmissive ? "yes" : "no") + ")");
        }
    }
    
    // If no embedded textures, use external texture path
    if (!material) {
        std::string finalTexturePath = texturePath;
        std::string modelDir = objPath.substr(0, objPath.find_last_of("/\\") + 1);
        
        if (finalTexturePath.empty()) {
            // Try external textures from FBX/GLTF
            if (!fbxTexturePaths.empty() && !fbxTexturePaths[LoadedModel::ALBEDO].empty()) {
                // Skip embedded texture references (starting with '*')
                if (fbxTexturePaths[LoadedModel::ALBEDO][0] != '*') {
                    finalTexturePath = fbxTexturePaths[LoadedModel::ALBEDO];
                    DebugManager::GetInstance().Log("SceneAssetLoader", 
                        "Using external texture: " + finalTexturePath);
                }
            }
            
            // Fall back to MTL file (for OBJ files)
            if (finalTexturePath.empty()) {
                std::string mtlPath = objPath.substr(0, objPath.find_last_of('.')) + ".mtl";
                std::string mtlTexturePath = ParseMTLFile(mtlPath);
                
                if (!mtlTexturePath.empty()) {
                    if (mtlTexturePath.find("textures/") == 0) {
                        mtlTexturePath = "Texture/" + mtlTexturePath.substr(9);
                    }
                    finalTexturePath = modelDir + mtlTexturePath;
                    DebugManager::GetInstance().Log("SceneAssetLoader", 
                        "Found texture from MTL: " + finalTexturePath);
                }
            }
        }

        // Create material from file path
        material = CreateMaterialResource(device, finalTexturePath);
    }
    
    if (!material) {
        DebugManager::GetInstance().Log("SceneAssetLoader", "Failed to create material");
        return entt::null;
    }

    // Create entity
    entt::entity entity = registry.create();

    // Add Transform component
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = position;
    transform.scale = scale;
    transform.rotation = DirectX::XMFLOAT4(0, 0, 0, 1);

    // Add Mesh component
    registry.emplace<MeshComponent>(entity, mesh, material);

    // Add Render Priority component
    auto& priority = registry.emplace<RenderPriorityComponent>(entity);
    priority.sortKey = 1000;
    priority.renderPass = 0;  // Opaque

    // 【已移除】物理设置 - 所有物理必须由调用方手动创建
    if (physicsOpts) {
        DebugManager::GetInstance().Log("SceneAssetLoader", 
            "WARNING: PhysicsOptions provided but IGNORED - physics must be created manually");
    }

    DebugManager::GetInstance().Log("SceneAssetLoader", 
        "Loaded entity from " + objPath + " at (" + 
        std::to_string(position.x) + ", " + 
        std::to_string(position.y) + ", " + 
        std::to_string(position.z) + ")");

    return entity;
}

int SceneAssetLoader::LoadSceneFromFile(
    entt::registry& registry,
    std::shared_ptr<Scene> scene,
    ID3D11Device* device,
    const std::string& sceneFilePath
) {
    std::ifstream file(sceneFilePath);
    if (!file.is_open()) {
        DebugManager::GetInstance().Log("SceneAssetLoader", "Failed to open scene file: " + sceneFilePath);
        return 0;
    }

    // TODO: Implement JSON parsing
    DebugManager::GetInstance().Log("SceneAssetLoader", "Scene file loading not yet implemented");
    return 0;
}

std::shared_ptr<Mesh> SceneAssetLoader::LoadMeshResource(const std::string& objPath) {
    std::string ext = GetFileExtension(objPath);
    
    if (ext == "fbx" || ext == "gltf" || ext == "glb" || ext == "dae" || ext == "blend" || ext == "3ds") {
        LoadedModel model;
        if (!AssimpLoader::LoadFromFile(objPath, model)) {
            DebugManager::GetInstance().Log("SceneAssetLoader", "Assimp loading failed: " + objPath);
            return nullptr;
        }
        
        DebugManager::GetInstance().Log("SceneAssetLoader", 
            "Loaded mesh (Assimp) with " + std::to_string(model.mesh->GetVertices().size()) + " vertices");
        
        return model.mesh;
    }
    
    auto mesh = std::make_shared<Mesh>();
    
    if (!OBJLoader::LoadFromFile(objPath, *mesh)) {
        DebugManager::GetInstance().Log("SceneAssetLoader", "OBJ loading failed: " + objPath);
        return nullptr;
    }

    DebugManager::GetInstance().Log("SceneAssetLoader", 
        "Loaded mesh with " + std::to_string(mesh->GetVertices().size()) + " vertices");

    return mesh;
}

std::shared_ptr<Material> SceneAssetLoader::CreateMaterialResource(
    ID3D11Device* device,
    const std::string& texturePath
) {
    auto material = std::make_shared<Material>();
    
    // Set default PBR values
    material->albedo = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    material->metallic = 0.0f;
    material->roughness = 0.5f;
    material->ao = 1.0f;
    material->isTransparent = false;

    // Load texture if provided
    if (!texturePath.empty()) {
        material->albedoTexture = texturePath;
        
        ID3D11ShaderResourceView* textureSRV = nullptr;
        if (TextureLoader::LoadFromFile(device, texturePath, &textureSRV)) {
            material->albedoTextureSRV = textureSRV;
            material->shaderProgram = textureSRV;
            
            DebugManager::GetInstance().Log("SceneAssetLoader", 
                "Texture loaded successfully: " + texturePath);
        } else {
            DebugManager::GetInstance().Log("SceneAssetLoader", 
                "Failed to load texture: " + texturePath);
            material->albedoTextureSRV = nullptr;
            material->shaderProgram = nullptr;
        }
    } else {
        material->albedoTextureSRV = nullptr;
        material->shaderProgram = nullptr;
    }

    return material;
}

std::shared_ptr<Material> SceneAssetLoader::CreatePBRMaterial(
    ID3D11Device* device,
    const std::string& albedoPath,
    const std::string& normalPath,
    const std::string& metallicPath,
    const std::string& roughnessPath
) {
    auto material = std::make_shared<Material>();
    
    material->albedo = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    material->metallic = 0.0f;
    material->roughness = 0.5f;
    material->ao = 1.0f;
    material->isTransparent = false;
    
    // Helper lambda to load texture
    auto loadTexture = [&](const std::string& path, void** outSRV, std::string& outPath) -> bool {
        if (path.empty()) return false;
        
        outPath = path;
        ID3D11ShaderResourceView* srv = nullptr;
        if (TextureLoader::LoadFromFile(device, path, &srv)) {
            *outSRV = srv;
            return true;
        }
        return false;
    };
    
    loadTexture(albedoPath, &material->albedoTextureSRV, material->albedoTexture);
    loadTexture(normalPath, &material->normalTextureSRV, material->normalTexture);
    loadTexture(metallicPath, &material->metallicTextureSRV, material->metallicTexture);
    loadTexture(roughnessPath, &material->roughnessTextureSRV, material->roughnessTexture);
    
    material->shaderProgram = material->albedoTextureSRV;
    
    return material;
}

std::shared_ptr<Material> SceneAssetLoader::CreateMaterialFromEmbedded(
    ID3D11Device* device,
    const std::vector<EmbeddedTexture>& embeddedTextures
) {
    auto material = std::make_shared<Material>();
    
    material->albedo = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    material->metallic = 0.0f;
    material->roughness = 0.5f;
    material->ao = 1.0f;
    material->isTransparent = false;
    
    std::cout << "[SceneAssetLoader] CreateMaterialFromEmbedded called with " 
              << embeddedTextures.size() << " texture slots" << std::endl;
    
    // Helper lambda to load embedded texture
    auto loadEmbeddedTexture = [&](size_t index, void** outSRV, std::string& outPath) -> bool {
        if (index >= embeddedTextures.size() || embeddedTextures[index].data.empty()) {
            return false;
        }
        
        const auto& tex = embeddedTextures[index];
        ID3D11ShaderResourceView* srv = nullptr;
        
        std::cout << "[SceneAssetLoader] Loading embedded texture slot " << index 
                  << " (size=" << tex.data.size() << " bytes)" << std::endl;
        
        if (AssimpLoader::CreateTextureFromEmbedded(device, tex, &srv)) {
            *outSRV = srv;
            outPath = "[embedded:" + std::to_string(index) + "]";
            std::cout << "[SceneAssetLoader] Successfully created SRV for slot " << index << std::endl;
            return true;
        }
        std::cout << "[SceneAssetLoader] Failed to create SRV for slot " << index << std::endl;
        return false;
    };
    
    // Load PBR textures from embedded data
    loadEmbeddedTexture(LoadedModel::ALBEDO, &material->albedoTextureSRV, material->albedoTexture);
    loadEmbeddedTexture(LoadedModel::NORMAL, &material->normalTextureSRV, material->normalTexture);
    loadEmbeddedTexture(LoadedModel::METALLIC, &material->metallicTextureSRV, material->metallicTexture);
    loadEmbeddedTexture(LoadedModel::ROUGHNESS, &material->roughnessTextureSRV, material->roughnessTexture);
    loadEmbeddedTexture(LoadedModel::EMISSIVE, &material->emissiveTextureSRV, material->emissiveTexture);
    
    // Check if we have emissive texture - set emissive flag and strength
    if (material->emissiveTextureSRV) {
        material->isEmissive = true;
        material->emissiveStrength = 1.0f;
        material->emissiveColor = { 1.0f, 1.0f, 1.0f };
        std::cout << "[SceneAssetLoader] Emissive texture found! Setting isEmissive=true" << std::endl;
    }
    
    // Check if we got at least an albedo texture
    if (!material->albedoTextureSRV) {
        std::cout << "[SceneAssetLoader] Warning: No albedo texture loaded" << std::endl;
        
        // 如果有 emissive 但没有 albedo，把 emissive 也设置为 albedo（显示颜色）
        if (material->emissiveTextureSRV) {
            material->albedoTextureSRV = material->emissiveTextureSRV;
            material->albedoTexture = material->emissiveTexture;
            std::cout << "[SceneAssetLoader] Using emissive texture as albedo fallback" << std::endl;
        }
    }
    
    material->shaderProgram = material->albedoTextureSRV;
    
    std::cout << "[SceneAssetLoader] Created material: albedo=" 
              << (material->albedoTextureSRV ? "yes" : "no")
              << ", emissive=" << (material->emissiveTextureSRV ? "yes" : "no")
              << ", isEmissive=" << (material->isEmissive ? "true" : "false") << std::endl;
    
    return material;
}

std::string SceneAssetLoader::ParseMTLFile(const std::string& mtlPath) {
    std::ifstream file(mtlPath);
    if (!file.is_open()) {
        return "";
    }

    std::string line;
    while (std::getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.rfind("map_Kd ", 0) == 0) {
            std::string texPath = line.substr(7);
            texPath.erase(0, texPath.find_first_not_of(" \t"));
            return texPath;
        }
    }

    return "";
}

} // namespace outer_wilds
