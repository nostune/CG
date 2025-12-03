#pragma once
#include <entt/entt.hpp>
#include <DirectXMath.h>
#include <string>
#include <memory>
#include <d3d11.h>

namespace outer_wilds {

// Forward declarations
class Scene;
namespace resources {
    class Mesh;
    class Material;
}

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
     * @param texturePath Path to texture file (optional)
     * @param position World position
     * @param scale Scale factor
     * @return Entity ID
     */
    static entt::entity LoadModelAsEntity(
        entt::registry& registry,
        std::shared_ptr<Scene> scene,
        ID3D11Device* device,
        const std::string& objPath,
        const std::string& texturePath,
        const DirectX::XMFLOAT3& position = {0, 0, 0},
        const DirectX::XMFLOAT3& scale = {1, 1, 1}
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
     * Create shared material resource
     */
    static std::shared_ptr<resources::Material> CreateMaterialResource(
        ID3D11Device* device,
        const std::string& texturePath
    );
};

} // namespace outer_wilds
