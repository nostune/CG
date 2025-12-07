#include "SceneAssetLoader.h"
#include "Scene.h"
#include "components/TransformComponent.h"
#include "../graphics/components/MeshComponent.h"
#include "../graphics/components/RenderableComponent.h"
#include "../graphics/components/RenderPriorityComponent.h"
#include "../graphics/resources/OBJLoader.h"
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
    // Load mesh resource
    auto mesh = LoadMeshResource(objPath);
    if (!mesh) {
        DebugManager::GetInstance().Log("SceneAssetLoader", "Failed to load mesh: " + objPath);
        return entt::null;
    }

    // Create mesh GPU buffers
    mesh->CreateGPUBuffers(device);

    // ============================================
    // TEXTURE PATH RESOLUTION STRATEGY
    // ============================================
    // 1. If texturePath provided explicitly → use it directly
    // 2. If empty → try to parse from .mtl file (Blender export)
    // 3. MTL paths are relative, need to fix "textures/" → "Texture/"
    // ============================================
    
    std::string finalTexturePath = texturePath;
    if (finalTexturePath.empty()) {
        // Try to find .mtl file with same name as .obj
        std::string mtlPath = objPath.substr(0, objPath.find_last_of('.')) + ".mtl";
        std::string mtlTexturePath = ParseMTLFile(mtlPath);
        
        if (!mtlTexturePath.empty()) {
            // MTL paths are relative to .obj directory
            std::string objDir = objPath.substr(0, objPath.find_last_of("/\\") + 1);
            
            // ============================================
            // CRITICAL FIX: Blender exports use lowercase "textures/"
            // but our project structure uses "Texture/" (capital T)
            // ============================================
            if (mtlTexturePath.find("textures/") == 0) {
                // Replace "textures/" with "Texture/" to match project structure
                mtlTexturePath = "Texture/" + mtlTexturePath.substr(9);
            }
            
            finalTexturePath = objDir + mtlTexturePath;
            
            DebugManager::GetInstance().Log("SceneAssetLoader", 
                "Found texture from MTL: " + finalTexturePath);
        } else {
            DebugManager::GetInstance().Log("SceneAssetLoader", 
                "Warning: No texture found in MTL file");
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
            
            DebugManager::GetInstance().Log("SceneAssetLoader", 
                "✅ Texture loaded successfully: " + texturePath);
        } else {
            DebugManager::GetInstance().Log("SceneAssetLoader", 
                "❌ Failed to load texture: " + texturePath + ", using default material");
        }
    } else {
        DebugManager::GetInstance().Log("SceneAssetLoader", 
            "⚠️ No texture path provided, using default white material");
    }

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
