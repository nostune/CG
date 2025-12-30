#pragma once
#include "Mesh.h"
#include "Material.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <d3d11.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace outer_wilds {
namespace resources {

/**
 * @brief Multi-format 3D model loader using Assimp
 * 
 * Supports: FBX, GLTF, GLB, OBJ, DAE, BLEND, 3DS, etc.
 * Features:
 * - Automatic multi-texture extraction (albedo, normal, metallic, roughness)
 * - Embedded texture support (critical for GLB format)
 * - Tangent space calculation for normal mapping
 * - UV coordinate handling
 * - PBR material support
 */

/**
 * @brief Embedded texture data (from GLB/GLTF)
 */
struct EmbeddedTexture {
    std::vector<unsigned char> data;    // Raw texture data (PNG/JPG bytes or RGBA pixels)
    uint32_t width = 0;                 // Width (0 if compressed format like PNG)
    uint32_t height = 0;                // Height (0 if compressed format like PNG)
    std::string formatHint;             // Format hint: "png", "jpg", "rgba", etc.
    bool isCompressed = true;           // true for PNG/JPG, false for raw RGBA
};

/**
 * @brief Model bounding box information
 */
struct ModelBounds {
    DirectX::XMFLOAT3 min = { FLT_MAX, FLT_MAX, FLT_MAX };
    DirectX::XMFLOAT3 max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    DirectX::XMFLOAT3 center = { 0, 0, 0 };
    DirectX::XMFLOAT3 size = { 0, 0, 0 };
    float radius = 0;  // Bounding sphere radius
};

/**
 * @brief Sub-mesh with its own material (for multi-material models)
 */
struct SubMesh {
    std::shared_ptr<Mesh> mesh;
    std::string materialName;
    int materialIndex = -1;
    
    // Texture paths for this submesh's material
    std::vector<std::string> texturePaths;  // [albedo, normal, metallic, roughness, ao, emissive]
    std::vector<EmbeddedTexture> embeddedTextures;
};

/**
 * @brief Multi-material model data
 */
struct MultiMaterialModel {
    std::vector<SubMesh> subMeshes;
    ModelBounds bounds;
    
    // Collision mesh data (if exists)
    std::vector<Vertex> collisionVertices;
    std::vector<uint32_t> collisionIndices;
    bool hasCollisionMesh = false;
    
    // Texture type indices (same as LoadedModel)
    enum TextureType { ALBEDO = 0, NORMAL = 1, METALLIC = 2, ROUGHNESS = 3, AO = 4, EMISSIVE = 5, MAX_TEXTURES = 6 };
};

struct LoadedModel {
    std::shared_ptr<Mesh> mesh;
    std::vector<std::string> texturePaths;          // External texture paths [albedo, normal, metallic, roughness]
    std::vector<EmbeddedTexture> embeddedTextures;  // Embedded textures (for GLB)
    ModelBounds bounds;                              // Model bounding box
    
    // Collision mesh data (if exists)
    std::vector<Vertex> collisionVertices;
    std::vector<uint32_t> collisionIndices;
    bool hasCollisionMesh = false;
    
    // Texture type indices
    enum TextureType { ALBEDO = 0, NORMAL = 1, METALLIC = 2, ROUGHNESS = 3, AO = 4, EMISSIVE = 5, MAX_TEXTURES = 6 };
    
    // Check if model has embedded texture at index
    bool HasEmbeddedTexture(size_t index) const {
        return index < embeddedTextures.size() && !embeddedTextures[index].data.empty();
    }
    
    // Check if model has external texture path at index
    bool HasExternalTexture(size_t index) const {
        return index < texturePaths.size() && !texturePaths[index].empty();
    }
};

/**
 * @brief Options for model loading
 */
struct ModelLoadOptions {
    bool skipBoundsCalculation = false;  // Skip bounding box calculation (for known-scale models)
    bool verbose = true;                  // Print loading messages
    bool fastLoad = false;                // Use fast loading (skip tangent/normal generation)
};

class AssimpLoader {
public:
    /**
     * @brief Load model from file (supports FBX, GLTF, GLB, OBJ, etc.)
     * @param filePath Path to model file
     * @param outModel Output model data with mesh and textures
     * @param options Loading options (optional)
     * @return true if successful
     */
    static bool LoadFromFile(const std::string& filePath, LoadedModel& outModel, 
                             const ModelLoadOptions& options = {});

    /**
     * @brief Load multi-material model from file
     * Each material gets its own sub-mesh for proper texture mapping
     * @param filePath Path to model file
     * @param outModel Output multi-material model data
     * @return true if successful
     */
    static bool LoadMultiMaterialModel(const std::string& filePath, MultiMaterialModel& outModel);

    /**
     * @brief Create GPU texture from embedded texture data
     * @param device D3D11 device
     * @param embeddedTex Embedded texture data
     * @param outTexture Output shader resource view
     * @return true if successful
     */
    static bool CreateTextureFromEmbedded(
        ID3D11Device* device,
        const EmbeddedTexture& embeddedTex,
        ID3D11ShaderResourceView** outTexture
    );

private:
    static void ProcessNode(aiNode* node, const aiScene* scene, 
                           std::vector<Vertex>& vertices, 
                           std::vector<uint32_t>& indices,
                           std::vector<Vertex>& collisionVertices,
                           std::vector<uint32_t>& collisionIndices);
    
    static void ProcessMesh(aiMesh* mesh, const aiScene* scene,
                           std::vector<Vertex>& vertices,
                           std::vector<uint32_t>& indices);
    
    /**
     * @brief Process node for multi-material model
     * Groups meshes by material index
     */
    static void ProcessNodeMultiMaterial(aiNode* node, const aiScene* scene,
                                         std::map<int, std::pair<std::vector<Vertex>, std::vector<uint32_t>>>& materialMeshes,
                                         std::vector<Vertex>& collisionVertices,
                                         std::vector<uint32_t>& collisionIndices);
    
    /**
     * @brief Extract texture paths for a specific material
     */
    static std::vector<std::string> ExtractMaterialTexturePaths(aiMaterial* material, 
                                                                 const std::string& modelDir);
    
    static std::vector<std::string> ExtractTexturePaths(const aiScene* scene, 
                                                        const std::string& modelDir);
    
    /**
     * @brief Extract embedded textures from GLTF/GLB scene
     */
    static std::vector<EmbeddedTexture> ExtractEmbeddedTextures(
        const aiScene* scene,
        const std::string& modelDir
    );
    
    /**
     * @brief Extract embedded textures for a specific material
     */
    static std::vector<EmbeddedTexture> ExtractMaterialEmbeddedTextures(
        const aiScene* scene,
        aiMaterial* material
    );
    
    /**
     * @brief Get texture path or embedded index for a material texture type
     * @return Path string or "*N" for embedded texture index N
     */
    static std::string GetTextureReference(
        aiMaterial* material,
        aiTextureType type,
        const aiScene* scene,
        const std::string& modelDir
    );
};

} // namespace resources
} // namespace outer_wilds
