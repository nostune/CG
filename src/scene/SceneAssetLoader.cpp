#include "SceneAssetLoader.h"
#include "Scene.h"
#include "components/TransformComponent.h"
#include "../graphics/components/MeshComponent.h"
#include "../graphics/components/RenderableComponent.h"
#include "../graphics/components/RenderPriorityComponent.h"
#include "../graphics/resources/OBJLoader.h"
#include "../graphics/resources/TextureLoader.h"
#include "../core/DebugManager.h"
#include <fstream>

namespace outer_wilds {

using namespace components;
using namespace resources;

entt::entity SceneAssetLoader::LoadModelAsEntity(
    entt::registry& registry,
    std::shared_ptr<Scene> scene,
    ID3D11Device* device,
    const std::string& objPath,
    const std::string& texturePath,
    const DirectX::XMFLOAT3& position,
    const DirectX::XMFLOAT3& scale
) {
    // Load mesh resource
    auto mesh = LoadMeshResource(objPath);
    if (!mesh) {
        DebugManager::GetInstance().Log("SceneAssetLoader", "Failed to load mesh: " + objPath);
        return entt::null;
    }

    // Create mesh GPU buffers
    mesh->CreateGPUBuffers(device);

    // Create material resource
    auto material = CreateMaterialResource(device, texturePath);
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
    transform.rotation = DirectX::XMFLOAT4(0, 0, 0, 1); // Quaternion identity

    // Add Mesh component
    registry.emplace<MeshComponent>(entity, mesh, material);

    // Add Renderable component (if needed for compatibility)
    // Note: RenderableComponent doesn't have isVisible/castShadows, so we skip it
    // MeshComponent already has these fields

    // Add Render Priority component
    auto& priority = registry.emplace<RenderPriorityComponent>(entity);
    priority.sortKey = 1000; // Default priority
    priority.renderPass = 0; // Opaque

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
    // This is a placeholder for JSON/XML scene file loading
    // For production, you would:
    // 1. Parse JSON/XML scene definition
    // 2. Iterate through objects
    // 3. Call LoadModelAsEntity for each object
    // 4. Support instancing for repeated objects
    
    std::ifstream file(sceneFilePath);
    if (!file.is_open()) {
        DebugManager::GetInstance().Log("SceneAssetLoader", "Failed to open scene file: " + sceneFilePath);
        return 0;
    }

    // TODO: Implement JSON parsing
    // For now, return 0
    DebugManager::GetInstance().Log("SceneAssetLoader", "Scene file loading not yet implemented");
    return 0;
}

std::shared_ptr<Mesh> SceneAssetLoader::LoadMeshResource(const std::string& objPath) {
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
        
        // Load texture to GPU (store in material for renderer to use)
        ID3D11ShaderResourceView* textureSRV = nullptr;
        if (TextureLoader::LoadFromFile(device, texturePath, &textureSRV)) {
            // Store texture handle in material
            // Note: In production, you'd use a resource manager to avoid duplicates
            material->shaderProgram = textureSRV; // Temporary storage
        } else {
            DebugManager::GetInstance().Log("SceneAssetLoader", 
                "Failed to load texture: " + texturePath + ", using default material");
        }
    }

    return material;
}

} // namespace outer_wilds
