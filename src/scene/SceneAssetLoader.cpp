#include "SceneAssetLoader.h"
#include "Scene.h"
#include "components/TransformComponent.h"
#include "../graphics/components/MeshComponent.h"
#include "../graphics/components/RenderableComponent.h"
#include "../graphics/components/RenderPriorityComponent.h"
#include "../graphics/resources/OBJLoader.h"
#include "../graphics/resources/AssimpLoader.h"
#include "../graphics/resources/TextureLoader.h"
#include "../physics/components/ColliderComponent.h"
#include "../physics/components/RigidBodyComponent.h"
#include "../physics/PhysXManager.h"
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
    const PhysicsOptions* physicsOpts
) {
    std::string ext = GetFileExtension(objPath);
    std::shared_ptr<Mesh> mesh = nullptr;
    std::vector<std::string> fbxTexturePaths;  // Textures from FBX/GLTF
    
    // ============================================
    // LOAD MESH BASED ON FILE FORMAT
    // ============================================
    if (ext == "fbx" || ext == "gltf" || ext == "glb" || ext == "dae" || ext == "blend" || ext == "3ds") {
        // Use AssimpLoader for FBX and other formats
        LoadedModel model;
        if (!AssimpLoader::LoadFromFile(objPath, model)) {
            DebugManager::GetInstance().Log("SceneAssetLoader", "Assimp loading failed: " + objPath);
            return entt::null;
        }
        mesh = model.mesh;
        fbxTexturePaths = model.texturePaths;  // Get embedded texture paths
        
        DebugManager::GetInstance().Log("SceneAssetLoader", 
            "Loaded mesh (Assimp) with " + std::to_string(mesh->GetVertices().size()) + " vertices, " +
            std::to_string(fbxTexturePaths.size()) + " textures from model");
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
    // TEXTURE PATH RESOLUTION STRATEGY
    // ============================================
    // Priority order:
    // 1. If texturePath provided explicitly → use it directly
    // 2. If FBX/GLTF → use textures extracted from model
    // 3. If OBJ → try to parse from .mtl file
    // ============================================
    
    std::string finalTexturePath = texturePath;
    std::string modelDir = objPath.substr(0, objPath.find_last_of("/\\") + 1);
    
    if (finalTexturePath.empty()) {
        // Try FBX embedded textures first
        if (!fbxTexturePaths.empty() && !fbxTexturePaths[LoadedModel::ALBEDO].empty()) {
            finalTexturePath = fbxTexturePaths[LoadedModel::ALBEDO];
            DebugManager::GetInstance().Log("SceneAssetLoader", 
                "Using texture from FBX: " + finalTexturePath);
        } else {
            // Fall back to MTL file (for OBJ files)
            std::string mtlPath = objPath.substr(0, objPath.find_last_of('.')) + ".mtl";
            std::string mtlTexturePath = ParseMTLFile(mtlPath);
            
            if (!mtlTexturePath.empty()) {
                // MTL paths are relative to .obj directory
                if (mtlTexturePath.find("textures/") == 0) {
                    mtlTexturePath = "Texture/" + mtlTexturePath.substr(9);
                }
                finalTexturePath = modelDir + mtlTexturePath;
                DebugManager::GetInstance().Log("SceneAssetLoader", 
                    "Found texture from MTL: " + finalTexturePath);
            } else {
                // Last resort: look for common texture file patterns in model directory
                std::vector<std::string> commonPatterns = {
                    "texture_diffuse_00.png",
                    "diffuse.png",
                    "albedo.png",
                    "base_color.png"
                };
                for (const auto& pattern : commonPatterns) {
                    std::string testPath = modelDir + pattern;
                    std::ifstream testFile(testPath);
                    if (testFile.good()) {
                        finalTexturePath = testPath;
                        DebugManager::GetInstance().Log("SceneAssetLoader", 
                            "Found texture by pattern: " + finalTexturePath);
                        break;
                    }
                }
            }
        }
    }

    // Create material resource
    auto material = CreateMaterialResource(device, finalTexturePath);
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

    // Add Render Priority component
    auto& priority = registry.emplace<RenderPriorityComponent>(entity);
    priority.sortKey = 1000; // Default priority
    priority.renderPass = 0; // Opaque

    // ============================================
    // PHYSICS SETUP (Optional)
    // ============================================
    // OBJ files DO NOT contain physics information!
    // All physics properties must be set in code via PhysicsOptions:
    // - Collider shape (Box, Sphere, ConvexMesh)
    // - Mass, friction, restitution
    // - Gravity, kinematic flags
    // ============================================
    if (physicsOpts && (physicsOpts->addCollider || physicsOpts->addRigidBody)) {
        auto& physxManager = PhysXManager::GetInstance();
        physx::PxPhysics* physics = physxManager.GetPhysics();
        physx::PxScene* pxScene = physxManager.GetScene();

        // Create physics material
        physx::PxMaterial* pxMaterial = physics->createMaterial(
            physicsOpts->staticFriction,
            physicsOpts->dynamicFriction,
            physicsOpts->restitution
        );

        // Create PhysX shape based on collider type
        physx::PxShape* pxShape = nullptr;
        if (physicsOpts->shape == PhysicsOptions::ColliderShape::Sphere) {
            float maxScale = (scale.x > scale.y) ? scale.x : scale.y;
            maxScale = (maxScale > scale.z) ? maxScale : scale.z;
            float radius = physicsOpts->sphereRadius * maxScale;
            pxShape = physics->createShape(physx::PxSphereGeometry(radius), *pxMaterial);
        } else if (physicsOpts->shape == PhysicsOptions::ColliderShape::Box) {
            physx::PxVec3 halfExtents(
                physicsOpts->boxExtent.x * scale.x * 0.5f,
                physicsOpts->boxExtent.y * scale.y * 0.5f,
                physicsOpts->boxExtent.z * scale.z * 0.5f
            );
            pxShape = physics->createShape(physx::PxBoxGeometry(halfExtents), *pxMaterial);
        }

        if (pxShape) {
            // 确保 Shape 支持场景查询（CharacterController 需要这个）
            // 设置 Shape 的 flags 以确保可以被查询检测到
            pxShape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE, true);
            pxShape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, true);
            
            // 设置查询过滤数据（允许所有类型的查询）
            physx::PxFilterData queryFilterData;
            queryFilterData.word0 = 0xFFFFFFFF;  // 可以与所有分组碰撞
            queryFilterData.word1 = 0xFFFFFFFF;
            pxShape->setQueryFilterData(queryFilterData);
            
            // 设置模拟过滤数据
            physx::PxFilterData simFilterData;
            simFilterData.word0 = 0xFFFFFFFF;  // 可以与所有分组碰撞
            simFilterData.word1 = 0xFFFFFFFF;
            pxShape->setSimulationFilterData(simFilterData);
            
            DebugManager::GetInstance().Log("SceneAssetLoader", 
                "Shape created with SCENE_QUERY_SHAPE and SIMULATION_SHAPE flags enabled");
            
            // Add Collider component
            auto& collider = registry.emplace<ColliderComponent>(entity);
            if (physicsOpts->shape == PhysicsOptions::ColliderShape::Sphere) {
                collider.type = ColliderType::Sphere;
                collider.sphereRadius = physicsOpts->sphereRadius;
            } else if (physicsOpts->shape == PhysicsOptions::ColliderShape::Box) {
                collider.type = ColliderType::Box;
                collider.boxExtent = physicsOpts->boxExtent;
            }

            // Add RigidBody component (if requested)
            if (physicsOpts->addRigidBody) {
                physx::PxTransform pxTransform(
                    physx::PxVec3(position.x, position.y, position.z),
                    physx::PxQuat(0, 0, 0, 1)
                );

                physx::PxRigidActor* pxActor = nullptr;
                if (physicsOpts->isKinematic || physicsOpts->mass == 0.0f) {
                    // Static or kinematic body
                    pxActor = physics->createRigidStatic(pxTransform);
                } else {
                    // Dynamic body
                    physx::PxRigidDynamic* dynamicActor = physics->createRigidDynamic(pxTransform);
                    dynamicActor->setMass(physicsOpts->mass);
                    dynamicActor->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !physicsOpts->useGravity);
                    pxActor = dynamicActor;
                }

                pxActor->attachShape(*pxShape);
                pxScene->addActor(*pxActor);

                auto& rigidBody = registry.emplace<RigidBodyComponent>(entity);
                rigidBody.mass = physicsOpts->mass;
                rigidBody.useGravity = physicsOpts->useGravity;
                rigidBody.isKinematic = physicsOpts->isKinematic;
                rigidBody.physxActor = pxActor;

                DebugManager::GetInstance().Log("SceneAssetLoader", 
                    "Added physics: " + std::string(physicsOpts->isKinematic ? "Static" : "Dynamic") + 
                    " body with mass=" + std::to_string(physicsOpts->mass));
            }

            pxShape->release();
        }
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
    std::string ext = GetFileExtension(objPath);
    
    // Use AssimpLoader for FBX, GLTF, and other formats
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
    
    // Use OBJLoader for OBJ files (original behavior)
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

    // Load texture if provided (backward compatibility - single texture)
    if (!texturePath.empty()) {
        material->albedoTexture = texturePath;
        
        ID3D11ShaderResourceView* textureSRV = nullptr;
        if (TextureLoader::LoadFromFile(device, texturePath, &textureSRV)) {
            material->albedoTextureSRV = textureSRV;  // Use new multi-texture field
            material->shaderProgram = textureSRV;     // Keep for backward compatibility
            
            DebugManager::GetInstance().Log("SceneAssetLoader", 
                "Texture loaded successfully: " + texturePath + 
                ", SRV=" + std::to_string(reinterpret_cast<uintptr_t>(textureSRV)));
        } else {
            DebugManager::GetInstance().Log("SceneAssetLoader", 
                "Failed to load texture: " + texturePath + ", using default material");
            material->albedoTextureSRV = nullptr;
            material->shaderProgram = nullptr;
        }
    } else {
        material->albedoTextureSRV = nullptr;
        material->shaderProgram = nullptr;
        DebugManager::GetInstance().Log("SceneAssetLoader", 
            "No texture path provided, using default white material");
    }

    // Debug: confirm material state
    DebugManager::GetInstance().Log("SceneAssetLoader", 
        "Material created: albedoSRV=" + std::to_string(reinterpret_cast<uintptr_t>(material->albedoTextureSRV)) + 
        ", shaderProgram=" + std::to_string(reinterpret_cast<uintptr_t>(material->shaderProgram)));

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
    
    // Set default PBR values
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
            DebugManager::GetInstance().Log("SceneAssetLoader", "Loaded texture: " + path);
            return true;
        } else {
            DebugManager::GetInstance().Log("SceneAssetLoader", "Failed to load texture: " + path);
            return false;
        }
    };
    
    // Load all PBR textures
    loadTexture(albedoPath, &material->albedoTextureSRV, material->albedoTexture);
    loadTexture(normalPath, &material->normalTextureSRV, material->normalTexture);
    loadTexture(metallicPath, &material->metallicTextureSRV, material->metallicTexture);
    loadTexture(roughnessPath, &material->roughnessTextureSRV, material->roughnessTexture);
    
    // Backward compatibility
    material->shaderProgram = material->albedoTextureSRV;
    
    DebugManager::GetInstance().Log("SceneAssetLoader", 
        "Created PBR material with " + 
        std::to_string(!!(material->albedoTextureSRV) + !!(material->normalTextureSRV) + 
                       !!(material->metallicTextureSRV) + !!(material->roughnessTextureSRV)) + 
        " textures");
    
    return material;
}

std::string SceneAssetLoader::ParseMTLFile(const std::string& mtlPath) {
    std::ifstream file(mtlPath);
    if (!file.is_open()) {
        DebugManager::GetInstance().Log("SceneAssetLoader", "MTL file not found: " + mtlPath);
        return "";
    }

    std::string line;
    while (std::getline(file, line)) {
        // Remove leading/trailing whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        // Look for diffuse texture map (map_Kd)
        if (line.rfind("map_Kd ", 0) == 0) {
            std::string texPath = line.substr(7); // Skip "map_Kd "
            texPath.erase(0, texPath.find_first_not_of(" \t"));
            
            DebugManager::GetInstance().Log("SceneAssetLoader", 
                "Found map_Kd in MTL: " + texPath);
            return texPath;
        }
    }

    DebugManager::GetInstance().Log("SceneAssetLoader", "No map_Kd found in MTL: " + mtlPath);
    return "";
}

} // namespace outer_wilds
