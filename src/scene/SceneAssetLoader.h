#pragma once
#include <entt/entt.hpp>
#include <DirectXMath.h>
#include <string>
#include <memory>
#include <vector>
#include <d3d11.h>

namespace outer_wilds {

// Forward declarations
class Scene;
namespace resources {
    class Mesh;
    class Material;
    struct EmbeddedTexture;  // For GLB embedded textures
}

// Physics configuration for loaded models
struct PhysicsOptions {
    bool addCollider = false;
    bool addRigidBody = false;
    
    // Collider shape
    enum class ColliderShape { Box, Sphere, ConvexMesh } shape = ColliderShape::Sphere;
    DirectX::XMFLOAT3 boxExtent = { 1.0f, 1.0f, 1.0f };
    float sphereRadius = 1.0f;
    
    // RigidBody properties
    float mass = 1.0f;
    bool useGravity = true;
    bool isKinematic = false;
    float staticFriction = 0.5f;
    float dynamicFriction = 0.5f;
    float restitution = 0.3f;
};

/**
 * Scene Asset Loader
 * 
 * Handles loading of complex scenes with multiple objects following ECS pattern.
 * Each loaded object becomes an entity with appropriate components.
 * 
 * Usage for large scenes:
 * 1. Define scene layout in JSON/XML file
 * 2. Use LoadSceneFromFile() to batch load all objects
 * 3. Each object automatically gets Transform, Mesh, Material, and Renderable components
 * 4. Can load hundreds/thousands of entities efficiently
 * 
 * Alternative approaches:
 * - LoadModelAsEntity(): Load single OBJ model as one entity
 * - LoadModelWithMaterials(): Load OBJ with MTL material file
 * - CreatePrefabEntity(): Create reusable entity templates
 */
class SceneAssetLoader {
public:
    SceneAssetLoader() = default;
    ~SceneAssetLoader() = default;

    /**
     * Load a single 3D model as an entity
     * @param registry ECS registry
     * @param scene Scene to add entity to
     * @param objPath Path to .obj file
     * @param texturePath Path to texture file (optional, will parse from MTL if empty)
     * @param position World position
     * @param scale Scale factor
     * @param physicsOpts Physics configuration (nullptr = no physics)
     * @return Entity ID
     */
    static entt::entity LoadModelAsEntity(
        entt::registry& registry,
        std::shared_ptr<Scene> scene,
        ID3D11Device* device,
        const std::string& objPath,
        const std::string& texturePath = "",
        const DirectX::XMFLOAT3& position = {0, 0, 0},
        const DirectX::XMFLOAT3& scale = {1, 1, 1},
        const PhysicsOptions* physicsOpts = nullptr
    );

    /**
     * Load entire scene from file (for large scenes)
     * @param registry ECS registry
     * @param scene Scene to add entities to
     * @param sceneFilePath Path to scene definition file (JSON/XML)
     * @return Number of entities loaded
     * 
     * Scene file format example (JSON):
     * {
     *   "objects": [
     *     {
     *       "name": "sphere1",
     *       "model": "assets/BlendObj/planet1.obj",
     *       "texture": "assets/Texture/planet_UV.png",
     *       "position": [0, 0, 0],
     *       "rotation": [0, 0, 0],
     *       "scale": [1, 1, 1]
     *     }
     *   ]
     * }
     */
    static int LoadSceneFromFile(
        entt::registry& registry,
        std::shared_ptr<Scene> scene,
        ID3D11Device* device,
        const std::string& sceneFilePath
    );

    /**
     * Create shared mesh resource (for instancing multiple entities)
     * Useful for large scenes with repeated objects
     */
    static std::shared_ptr<resources::Mesh> LoadMeshResource(
        const std::string& objPath
    );

    /**
     * Create shared material resource with multi-texture support
     */
    static std::shared_ptr<resources::Material> CreateMaterialResource(
        ID3D11Device* device,
        const std::string& texturePath
    );
    
    /**
     * Create material with multiple PBR textures
     * @param device D3D11 device
     * @param albedoPath Path to albedo/diffuse texture
     * @param normalPath Path to normal map (optional)
     * @param metallicPath Path to metallic map (optional)
     * @param roughnessPath Path to roughness map (optional)
     */
    static std::shared_ptr<resources::Material> CreatePBRMaterial(
        ID3D11Device* device,
        const std::string& albedoPath,
        const std::string& normalPath = "",
        const std::string& metallicPath = "",
        const std::string& roughnessPath = ""
    );

    /**
     * Create material from embedded textures (for GLB/GLTF formats)
     * @param device D3D11 device
     * @param embeddedTextures Vector of embedded texture data
     * @return Shared pointer to material, or nullptr on failure
     */
    static std::shared_ptr<resources::Material> CreateMaterialFromEmbedded(
        ID3D11Device* device,
        const std::vector<resources::EmbeddedTexture>& embeddedTextures
    );

    /**
     * Parse MTL file to extract diffuse texture path
     * @param mtlPath Path to .mtl file
     * @return Path to diffuse texture (map_Kd), or empty string if not found
     */
    static std::string ParseMTLFile(
        const std::string& mtlPath
    );
};

} // namespace outer_wilds
