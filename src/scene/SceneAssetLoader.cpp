/**
 * SceneAssetLoader.cpp
 * 
 * 场景资源加载器 - 简化版（不包含物理设置）
 * 
 * 【重要】所有物理碰撞体必须在调用方手动创建
 * 本 loader 只负责加载模型和创建渲染组件
 */

// 禁用 Windows min/max 宏，避免与 std::min/max 冲突
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "SceneAssetLoader.h"
#include "Scene.h"
#include "components/TransformComponent.h"
#include "components/ChildEntityComponent.h"
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
#include <cmath>

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

// ========================================
// 带加载选项的版本（跳过包围盒计算等）
// ========================================
entt::entity SceneAssetLoader::LoadModelAsEntityWithOptions(
    entt::registry& registry,
    std::shared_ptr<Scene> scene,
    ID3D11Device* device,
    const std::string& objPath,
    const std::string& texturePath,
    const DirectX::XMFLOAT3& position,
    const DirectX::XMFLOAT3& scale,
    const ModelLoadingOptions& options
) {
    std::string ext = GetFileExtension(objPath);
    std::shared_ptr<Mesh> mesh = nullptr;
    std::vector<std::string> fbxTexturePaths;
    std::vector<EmbeddedTexture> embeddedTextures;
    
    // 设置 Assimp 加载选项
    resources::ModelLoadOptions assimpOptions;
    assimpOptions.skipBoundsCalculation = options.skipBoundsCalculation;
    assimpOptions.verbose = options.verbose;
    assimpOptions.fastLoad = options.fastLoad;  // 传递快速加载选项
    
    if (ext == "fbx" || ext == "gltf" || ext == "glb" || ext == "dae" || ext == "blend" || ext == "3ds") {
        LoadedModel model;
        if (!AssimpLoader::LoadFromFile(objPath, model, assimpOptions)) {
            DebugManager::GetInstance().Log("SceneAssetLoader", "Assimp loading failed: " + objPath);
            return entt::null;
        }
        mesh = model.mesh;
        fbxTexturePaths = model.texturePaths;
        embeddedTextures = model.embeddedTextures;
        
        if (options.verbose) {
            DebugManager::GetInstance().Log("SceneAssetLoader", 
                "Loaded mesh with options: " + std::to_string(mesh->GetVertices().size()) + " vertices" +
                (options.skipBoundsCalculation ? " (bounds skipped)" : ""));
        }
    } else {
        // OBJ 加载不支持选项，回退到标准加载
        return LoadModelAsEntity(registry, scene, device, objPath, texturePath, position, scale, nullptr);
    }
    
    if (!mesh) {
        return entt::null;
    }

    mesh->CreateGPUBuffers(device);

    // 纹理和材质处理
    // 【修复】优先使用传入的纹理路径，而非嵌入式纹理
    std::shared_ptr<Material> material = nullptr;
    std::string modelDir = objPath.substr(0, objPath.find_last_of("/\\") + 1);
    
    // 1. 优先使用传入的纹理路径
    if (!texturePath.empty()) {
        material = CreateMaterialResource(device, texturePath);
        if (material && options.verbose) {
            DebugManager::GetInstance().Log("SceneAssetLoader", 
                "Using provided texture: " + texturePath);
        }
    }
    
    // 2. 如果没有传入纹理，尝试嵌入式纹理
    if (!material) {
        bool hasEmbeddedAlbedo = !embeddedTextures.empty() && 
                                 embeddedTextures.size() > LoadedModel::ALBEDO &&
                                 !embeddedTextures[LoadedModel::ALBEDO].data.empty();
        bool hasEmbeddedEmissive = !embeddedTextures.empty() && 
                                   embeddedTextures.size() > LoadedModel::EMISSIVE &&
                                   !embeddedTextures[LoadedModel::EMISSIVE].data.empty();
        
        if (hasEmbeddedAlbedo || hasEmbeddedEmissive) {
            material = CreateMaterialFromEmbedded(device, embeddedTextures);
        }
    }
    
    // 3. 尝试 FBX 内部纹理路径
    if (!material && !fbxTexturePaths.empty() && 
        !fbxTexturePaths[LoadedModel::ALBEDO].empty() &&
        fbxTexturePaths[LoadedModel::ALBEDO][0] != '*') {
        material = CreateMaterialResource(device, fbxTexturePaths[LoadedModel::ALBEDO]);
    }
    
    if (!material) {
        material = std::make_shared<Material>();
        material->albedo = DirectX::XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f);
    }

    entt::entity entity = registry.create();

    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = position;
    transform.scale = scale;
    transform.rotation = DirectX::XMFLOAT4(0, 0, 0, 1);

    registry.emplace<MeshComponent>(entity, mesh, material);

    auto& priority = registry.emplace<RenderPriorityComponent>(entity);
    priority.sortKey = 1000;
    priority.renderPass = 0;

    if (options.verbose) {
        DebugManager::GetInstance().Log("SceneAssetLoader", 
            "Loaded entity with options from " + objPath);
    }

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
        material->emissiveStrength = 0.8f;  // 降低emissive强度到0.8，减少过亮导致的视觉问题
        material->emissiveColor = { 1.0f, 1.0f, 1.0f };
        std::cout << "[SceneAssetLoader] Emissive texture found! Setting isEmissive=true, strength=0.8" << std::endl;
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

// ============================================================================
// Multi-Material Model Loading
// ============================================================================

entt::entity SceneAssetLoader::LoadMultiMaterialModelAsEntities(
    entt::registry& registry,
    std::shared_ptr<Scene> scene,
    ID3D11Device* device,
    const std::string& modelPath,
    const std::string& textureDir,
    const DirectX::XMFLOAT3& position,
    const DirectX::XMFLOAT3& scale
) {
    DebugManager::GetInstance().Log("SceneAssetLoader", "Loading multi-material model: " + modelPath);
    
    // Load multi-material model
    MultiMaterialModel model;
    if (!AssimpLoader::LoadMultiMaterialModel(modelPath, model)) {
        DebugManager::GetInstance().Log("SceneAssetLoader", "Failed to load multi-material model: " + modelPath);
        return entt::null;
    }
    
    if (model.subMeshes.empty()) {
        DebugManager::GetInstance().Log("SceneAssetLoader", "No sub-meshes found in model");
        return entt::null;
    }
    
    // Determine texture directory
    std::string texDir = textureDir;
    if (texDir.empty()) {
        texDir = modelPath.substr(0, modelPath.find_last_of("/\\") + 1);
    }
    if (!texDir.empty() && texDir.back() != '/' && texDir.back() != '\\') {
        texDir += "\\";
    }
    
    bool isSciFiTrooper = modelPath.find("SciFiTrooperManV3") != std::string::npos;
    std::cout << "[SceneAssetLoader] isSciFiTrooper=" << isSciFiTrooper << ", texDir=" << texDir << std::endl;
    
    // Create main entity from FIRST submesh (this is the entity that will receive components)
    entt::entity mainEntity = entt::null;
    int subMeshIndex = 0;
    
    for (auto& subMesh : model.subMeshes) {
        std::cout << "[SceneAssetLoader] Processing subMesh " << subMeshIndex 
                  << ": " << subMesh.materialName << std::endl;
        
        // Create GPU buffers for mesh
        subMesh.mesh->CreateGPUBuffers(device);
        
        // Find texture for this material
        std::string albedoPath = "";
        
        if (isSciFiTrooper) {
            std::string matNameLower = subMesh.materialName;
            std::transform(matNameLower.begin(), matNameLower.end(), matNameLower.begin(), ::tolower);
            
            if (matNameLower.find("02") != std::string::npos) {
                albedoPath = texDir + "T_SciFiTrooperV3_bottom_a.png";
            } else if (matNameLower.find("03") != std::string::npos) {
                albedoPath = texDir + "T_SciFiTrooperV3_top_a.png";
            }
            std::cout << "[SceneAssetLoader] Matched texture: " << albedoPath << std::endl;
        }
        
        // Fallback to FBX embedded texture path
        if (albedoPath.empty() && !subMesh.texturePaths.empty() && 
            !subMesh.texturePaths[0].empty() && subMesh.texturePaths[0][0] != '*') {
            albedoPath = subMesh.texturePaths[0];
        }
        
        // Create material
        auto material = CreateMaterialResource(device, albedoPath);
        if (!material) {
            material = std::make_shared<Material>();
            material->albedo = DirectX::XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f);
        }
        
        // Create entity
        entt::entity entity = registry.create();
        
        auto& transform = registry.emplace<TransformComponent>(entity);
        transform.position = position;
        transform.scale = scale;
        // Initial rotation will be set by PlayerSystem based on gravity and look direction
        transform.rotation = DirectX::XMFLOAT4(0, 0, 0, 1);
        
        registry.emplace<MeshComponent>(entity, subMesh.mesh, material);
        
        auto& priority = registry.emplace<RenderPriorityComponent>(entity);
        priority.sortKey = 1000;
        priority.renderPass = 0;
        
        if (mainEntity == entt::null) {
            // First submesh becomes the main entity
            mainEntity = entity;
            std::cout << "[SceneAssetLoader] Created MAIN entity " << static_cast<uint32_t>(entity) << std::endl;
        } else {
            // Subsequent submeshes become child entities
            auto& childComp = registry.emplace<ChildEntityComponent>(entity);
            childComp.parent = mainEntity;
            std::cout << "[SceneAssetLoader] Created CHILD entity " << static_cast<uint32_t>(entity) 
                      << " -> parent " << static_cast<uint32_t>(mainEntity) << std::endl;
        }
        
        subMeshIndex++;
    }
    
    DebugManager::GetInstance().Log("SceneAssetLoader", 
        "Multi-material model loaded: " + std::to_string(model.subMeshes.size()) + " submeshes");
    
    return mainEntity;
}

// ========================================
// 【归一化加载方法】
// ========================================

bool SceneAssetLoader::GetModelBounds(
    const std::string& modelPath,
    resources::ModelBounds& outBounds
) {
    std::string ext = GetFileExtension(modelPath);
    
    if (ext == "fbx" || ext == "gltf" || ext == "glb" || ext == "dae" || ext == "blend" || ext == "3ds") {
        LoadedModel model;
        if (AssimpLoader::LoadFromFile(modelPath, model)) {
            outBounds = model.bounds;
            return true;
        }
    } else if (ext == "obj") {
        // OBJ文件需要手动计算边界
        auto mesh = std::make_shared<Mesh>();
        if (OBJLoader::LoadFromFile(modelPath, *mesh)) {
            const auto& vertices = mesh->GetVertices();
            outBounds.min = { FLT_MAX, FLT_MAX, FLT_MAX };
            outBounds.max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
            
            for (const auto& v : vertices) {
                if (v.position.x < outBounds.min.x) outBounds.min.x = v.position.x;
                if (v.position.y < outBounds.min.y) outBounds.min.y = v.position.y;
                if (v.position.z < outBounds.min.z) outBounds.min.z = v.position.z;
                if (v.position.x > outBounds.max.x) outBounds.max.x = v.position.x;
                if (v.position.y > outBounds.max.y) outBounds.max.y = v.position.y;
                if (v.position.z > outBounds.max.z) outBounds.max.z = v.position.z;
            }
            
            outBounds.size.x = outBounds.max.x - outBounds.min.x;
            outBounds.size.y = outBounds.max.y - outBounds.min.y;
            outBounds.size.z = outBounds.max.z - outBounds.min.z;
            outBounds.center.x = (outBounds.min.x + outBounds.max.x) * 0.5f;
            outBounds.center.y = (outBounds.min.y + outBounds.max.y) * 0.5f;
            outBounds.center.z = (outBounds.min.z + outBounds.max.z) * 0.5f;
            
            // 计算最大维度作为半径
            float maxDim = outBounds.size.x;
            if (outBounds.size.y > maxDim) maxDim = outBounds.size.y;
            if (outBounds.size.z > maxDim) maxDim = outBounds.size.z;
            outBounds.radius = maxDim * 0.5f;
            
            std::cout << "[SceneAssetLoader] OBJ bounds: size=(" << outBounds.size.x 
                      << ", " << outBounds.size.y << ", " << outBounds.size.z
                      << "), radius=" << outBounds.radius << std::endl;
            
            return true;
        }
    }
    
    return false;
}

entt::entity SceneAssetLoader::LoadModelWithDiameter(
    entt::registry& registry,
    std::shared_ptr<Scene> scene,
    ID3D11Device* device,
    const std::string& modelPath,
    const std::string& texturePath,
    const DirectX::XMFLOAT3& position,
    float targetDiameter,
    float* outActualRadius
) {
    return LoadModelWithRadius(registry, scene, device, modelPath, texturePath,
                               position, targetDiameter * 0.5f, outActualRadius);
}

entt::entity SceneAssetLoader::LoadModelWithRadius(
    entt::registry& registry,
    std::shared_ptr<Scene> scene,
    ID3D11Device* device,
    const std::string& modelPath,
    const std::string& texturePath,
    const DirectX::XMFLOAT3& position,
    float targetRadius,
    float* outActualRadius
) {
    // 1. 获取模型边界
    resources::ModelBounds bounds;
    if (!GetModelBounds(modelPath, bounds)) {
        DebugManager::GetInstance().Log("SceneAssetLoader", 
            "Failed to get model bounds for: " + modelPath);
        return entt::null;
    }
    
    // 2. 计算当前模型的最大半径（使用最大维度）
    float maxDimension = bounds.size.x;
    if (bounds.size.y > maxDimension) maxDimension = bounds.size.y;
    if (bounds.size.z > maxDimension) maxDimension = bounds.size.z;
    float currentRadius = maxDimension * 0.5f;
    
    if (currentRadius <= 0.001f) {
        DebugManager::GetInstance().Log("SceneAssetLoader", 
            "Model has invalid bounds (radius ~= 0): " + modelPath);
        return entt::null;
    }
    
    // 3. 计算缩放系数
    float scaleFactor = targetRadius / currentRadius;
    
    std::cout << "[SceneAssetLoader] Normalized loading: " << modelPath 
              << ", original_radius=" << currentRadius
              << ", target_radius=" << targetRadius
              << ", scale=" << scaleFactor << std::endl;
    
    // 4. 使用计算出的缩放系数加载模型
    DirectX::XMFLOAT3 scale = { scaleFactor, scaleFactor, scaleFactor };
    
    entt::entity entity = LoadModelAsEntity(
        registry, scene, device, modelPath, texturePath, position, scale, nullptr
    );
    
    // 5. 输出实际半径
    if (outActualRadius) {
        *outActualRadius = targetRadius;
    }
    
    return entity;
}

entt::entity SceneAssetLoader::LoadMultiMaterialModelWithRadius(
    entt::registry& registry,
    std::shared_ptr<Scene> scene,
    ID3D11Device* device,
    const std::string& modelPath,
    const std::string& textureDir,
    const DirectX::XMFLOAT3& position,
    float targetRadius,
    float* outActualRadius
) {
    // 1. 获取模型边界
    resources::ModelBounds bounds;
    if (!GetModelBounds(modelPath, bounds)) {
        DebugManager::GetInstance().Log("SceneAssetLoader", 
            "Failed to get model bounds for multi-material: " + modelPath);
        return entt::null;
    }
    
    // 2. 计算当前模型的最大半径
    float maxDimension = bounds.size.x;
    if (bounds.size.y > maxDimension) maxDimension = bounds.size.y;
    if (bounds.size.z > maxDimension) maxDimension = bounds.size.z;
    float currentRadius = maxDimension * 0.5f;
    
    if (currentRadius <= 0.001f) {
        DebugManager::GetInstance().Log("SceneAssetLoader", 
            "Multi-material model has invalid bounds: " + modelPath);
        return entt::null;
    }
    
    // 3. 计算缩放系数
    float scaleFactor = targetRadius / currentRadius;
    
    std::cout << "[SceneAssetLoader] Multi-material loading: " << modelPath 
              << ", original_radius=" << currentRadius
              << ", target_radius=" << targetRadius
              << ", scale=" << scaleFactor << std::endl;
    
    // 4. 使用多材质加载器加载模型
    MultiMaterialModel model;
    if (!AssimpLoader::LoadMultiMaterialModel(modelPath, model)) {
        DebugManager::GetInstance().Log("SceneAssetLoader", 
            "Failed to load multi-material model: " + modelPath);
        return entt::null;
    }
    
    if (model.subMeshes.empty()) {
        DebugManager::GetInstance().Log("SceneAssetLoader", "No sub-meshes found");
        return entt::null;
    }
    
    DirectX::XMFLOAT3 scale = { scaleFactor, scaleFactor, scaleFactor };
    
    // 创建主实体
    entt::entity mainEntity = registry.create();
    
    auto& mainTransform = registry.emplace<TransformComponent>(mainEntity);
    mainTransform.position = position;
    mainTransform.scale = scale;
    mainTransform.rotation = DirectX::XMFLOAT4(0, 0, 0, 1);
    
    // 为每个子 mesh 创建材质并添加到渲染列表
    // 使用 MultiMeshComponent 来存储多个 mesh
    auto& multiMesh = registry.emplace<MultiMeshComponent>(mainEntity);
    
    std::cout << "[SceneAssetLoader] Processing " << model.subMeshes.size() 
              << " sub-meshes for multi-material model" << std::endl;
    
    for (size_t i = 0; i < model.subMeshes.size(); i++) {
        auto& subMesh = model.subMeshes[i];
        
        // 创建 GPU 缓冲
        subMesh.mesh->CreateGPUBuffers(device);
        
        // 创建材质（优先使用嵌入纹理）
        std::shared_ptr<Material> material = nullptr;
        
        // 检查嵌入纹理
        if (!subMesh.embeddedTextures.empty()) {
            bool hasAlbedo = subMesh.embeddedTextures.size() > LoadedModel::ALBEDO &&
                            !subMesh.embeddedTextures[LoadedModel::ALBEDO].data.empty();
            bool hasEmissive = subMesh.embeddedTextures.size() > LoadedModel::EMISSIVE &&
                              !subMesh.embeddedTextures[LoadedModel::EMISSIVE].data.empty();
            
            if (hasAlbedo || hasEmissive) {
                material = CreateMaterialFromEmbedded(device, subMesh.embeddedTextures);
                std::cout << "[SceneAssetLoader] SubMesh " << i << " (" << subMesh.materialName 
                          << "): using embedded texture" << std::endl;
            }
        }
        
        // 如果没有嵌入纹理，尝试外部纹理
        if (!material && !subMesh.texturePaths.empty() && !subMesh.texturePaths[0].empty()) {
            if (subMesh.texturePaths[0][0] != '*') {  // 不是嵌入纹理引用
                material = CreateMaterialResource(device, subMesh.texturePaths[0]);
                std::cout << "[SceneAssetLoader] SubMesh " << i << " (" << subMesh.materialName 
                          << "): using external texture " << subMesh.texturePaths[0] << std::endl;
            }
        }
        
        // 如果还是没有材质，创建默认材质
        if (!material) {
            material = std::make_shared<Material>();
            material->albedo = DirectX::XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f);
            std::cout << "[SceneAssetLoader] SubMesh " << i << " (" << subMesh.materialName 
                      << "): using default gray material" << std::endl;
        }
        
        // 添加到多 mesh 组件
        multiMesh.meshes.push_back(subMesh.mesh);
        multiMesh.materials.push_back(material);
    }
    
    std::cout << "[SceneAssetLoader] Created multi-material entity with " 
              << multiMesh.meshes.size() << " sub-meshes" << std::endl;
    
    // 输出实际半径
    if (outActualRadius) {
        *outActualRadius = targetRadius;
    }
    
    return mainEntity;
}

} // namespace outer_wilds
