#pragma once
#include "Mesh.h"
#include "Material.h"
#include <string>
#include <vector>
#include <memory>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace outer_wilds {
namespace resources {

/**
 * @brief Multi-format 3D model loader using Assimp
 * 
 * Supports: FBX, GLTF, OBJ, DAE, BLEND, 3DS, etc.
 * Features:
 * - Automatic multi-texture extraction (albedo, normal, metallic, roughness)
 * - Tangent space calculation for normal mapping
 * - UV coordinate handling
 */
struct LoadedModel {
    std::shared_ptr<Mesh> mesh;
    std::vector<std::string> texturePaths;  // [albedo, normal, metallic, roughness]
    
    // Collision mesh data (if exists)
    std::vector<Vertex> collisionVertices;
    std::vector<uint32_t> collisionIndices;
    bool hasCollisionMesh = false;
    
    // Texture type indices
    enum TextureType { ALBEDO = 0, NORMAL = 1, METALLIC = 2, ROUGHNESS = 3 };
};

class AssimpLoader {
public:
    /**
     * @brief Load model from file (supports FBX, GLTF, OBJ, etc.)
     * @param filePath Path to model file
     * @param outModel Output model data with mesh and textures
     * @return true if successful
     */
    static bool LoadFromFile(const std::string& filePath, LoadedModel& outModel);

private:
    static void ProcessNode(aiNode* node, const aiScene* scene, 
                           std::vector<Vertex>& vertices, 
                           std::vector<uint32_t>& indices,
                           std::vector<Vertex>& collisionVertices,
                           std::vector<uint32_t>& collisionIndices);
    
    static void ProcessMesh(aiMesh* mesh, const aiScene* scene,
                           std::vector<Vertex>& vertices,
                           std::vector<uint32_t>& indices);
    
    static std::vector<std::string> ExtractTexturePaths(const aiScene* scene, 
                                                        const std::string& modelDir);
};

} // namespace resources
} // namespace outer_wilds
