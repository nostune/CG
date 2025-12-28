#include "AssimpLoader.h"
#include "TextureLoader.h"
#include "../../core/DebugManager.h"
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace outer_wilds {
namespace resources {

bool AssimpLoader::LoadFromFile(const std::string& filePath, LoadedModel& outModel) {
    std::cout << "[AssimpLoader] Starting load: " << filePath << std::endl;
    
    Assimp::Importer importer;
    
    // Determine file extension for format-specific handling
    std::string ext = filePath.substr(filePath.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    bool isGLTF = (ext == "gltf" || ext == "glb");
    
    std::cout << "[AssimpLoader] Calling ReadFile..." << std::endl;
    
    // Import flags - enhanced for GLB/GLTF support
    unsigned int flags = 
        aiProcess_Triangulate |              // Convert all primitives to triangles
        aiProcess_FlipUVs |                  // Flip UV coordinates for DirectX
        aiProcess_CalcTangentSpace |         // Calculate tangents for normal mapping
        aiProcess_GenSmoothNormals |         // Generate smooth normals if missing
        aiProcess_JoinIdenticalVertices;     // Optimize vertex count
    
    const aiScene* scene = importer.ReadFile(filePath, flags);
    
    std::cout << "[AssimpLoader] ReadFile returned" << std::endl;
    
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cout << "[AssimpLoader] ERROR: " << importer.GetErrorString() << std::endl;
        DebugManager::GetInstance().Log("AssimpLoader", 
            "Failed to load: " + std::string(importer.GetErrorString()));
        return false;
    }
    
    std::cout << "[AssimpLoader] Scene loaded. Meshes: " << scene->mNumMeshes 
              << ", Materials: " << scene->mNumMaterials
              << ", Textures: " << scene->mNumTextures << std::endl;
    
    // Extract directory path for texture resolution
    std::string modelDir = filePath.substr(0, filePath.find_last_of("/\\") + 1);
    
    // Process mesh data
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Vertex> collisionVertices;
    std::vector<uint32_t> collisionIndices;
    ProcessNode(scene->mRootNode, scene, vertices, indices, collisionVertices, collisionIndices);
    
    if (vertices.empty()) {
        DebugManager::GetInstance().Log("AssimpLoader", "No vertices found in model");
        return false;
    }
    
    // Calculate bounding box
    ModelBounds& bounds = outModel.bounds;
    bounds.min = { FLT_MAX, FLT_MAX, FLT_MAX };
    bounds.max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    
    for (const auto& vertex : vertices) {
        bounds.min.x = (std::min)(bounds.min.x, vertex.position.x);
        bounds.min.y = (std::min)(bounds.min.y, vertex.position.y);
        bounds.min.z = (std::min)(bounds.min.z, vertex.position.z);
        bounds.max.x = (std::max)(bounds.max.x, vertex.position.x);
        bounds.max.y = (std::max)(bounds.max.y, vertex.position.y);
        bounds.max.z = (std::max)(bounds.max.z, vertex.position.z);
    }
    
    bounds.size.x = bounds.max.x - bounds.min.x;
    bounds.size.y = bounds.max.y - bounds.min.y;
    bounds.size.z = bounds.max.z - bounds.min.z;
    bounds.center.x = (bounds.min.x + bounds.max.x) * 0.5f;
    bounds.center.y = (bounds.min.y + bounds.max.y) * 0.5f;
    bounds.center.z = (bounds.min.z + bounds.max.z) * 0.5f;
    bounds.radius = std::sqrt(bounds.size.x * bounds.size.x + 
                              bounds.size.y * bounds.size.y + 
                              bounds.size.z * bounds.size.z) * 0.5f;
    
    std::cout << "[AssimpLoader] Bounds: size=(" << bounds.size.x << ", " << bounds.size.y << ", " << bounds.size.z 
              << "), center=(" << bounds.center.x << ", " << bounds.center.y << ", " << bounds.center.z 
              << "), radius=" << bounds.radius << std::endl;
    
    // Create mesh object
    outModel.mesh = std::make_shared<Mesh>();
    outModel.mesh->SetVertices(vertices);
    outModel.mesh->SetIndices(indices);
    
    // Store collision mesh if found
    if (!collisionVertices.empty()) {
        outModel.collisionVertices = collisionVertices;
        outModel.collisionIndices = collisionIndices;
        outModel.hasCollisionMesh = true;
        std::cout << "[AssimpLoader] Found collision mesh: " << collisionVertices.size() << " vertices" << std::endl;
    }
    
    // Extract embedded textures (critical for GLB)
    outModel.embeddedTextures = ExtractEmbeddedTextures(scene, modelDir);
    
    // Extract texture paths (for external textures)
    outModel.texturePaths = ExtractTexturePaths(scene, modelDir);
    
    DebugManager::GetInstance().Log("AssimpLoader", 
        "Loaded: " + std::to_string(vertices.size()) + " vertices, " +
        std::to_string(indices.size() / 3) + " triangles, " +
        std::to_string(outModel.embeddedTextures.size()) + " embedded textures, " +
        std::to_string(scene->mNumMaterials) + " materials");
    
    return true;
}

bool AssimpLoader::CreateTextureFromEmbedded(
    ID3D11Device* device,
    const EmbeddedTexture& embeddedTex,
    ID3D11ShaderResourceView** outTexture
) {
    if (embeddedTex.data.empty()) {
        return false;
    }
    
    if (embeddedTex.isCompressed) {
        // Compressed format (PNG, JPG, etc.) - decode with WIC
        return TextureLoader::LoadFromMemory(
            device,
            embeddedTex.data.data(),
            embeddedTex.data.size(),
            embeddedTex.formatHint,
            outTexture
        );
    } else {
        // Raw RGBA data
        return TextureLoader::CreateFromRGBA(
            device,
            embeddedTex.data.data(),
            embeddedTex.width,
            embeddedTex.height,
            outTexture
        );
    }
}

void AssimpLoader::ProcessNode(aiNode* node, const aiScene* scene,
                               std::vector<Vertex>& vertices,
                               std::vector<uint32_t>& indices,
                               std::vector<Vertex>& collisionVertices,
                               std::vector<uint32_t>& collisionIndices) {
    // Process all meshes in this node
    for (uint32_t i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        
        // Check if this is a collision mesh (by name convention)
        std::string meshName = mesh->mName.C_Str();
        std::transform(meshName.begin(), meshName.end(), meshName.begin(), ::tolower);
        
        if (meshName.find("collision") != std::string::npos || 
            meshName.find("collider") != std::string::npos) {
            // This is a collision mesh
            ProcessMesh(mesh, scene, collisionVertices, collisionIndices);
            std::cout << "[AssimpLoader] Found collision mesh: " << meshName << std::endl;
        } else {
            // Regular render mesh
            ProcessMesh(mesh, scene, vertices, indices);
        }
    }
    
    // Recursively process child nodes
    for (uint32_t i = 0; i < node->mNumChildren; i++) {
        ProcessNode(node->mChildren[i], scene, vertices, indices, collisionVertices, collisionIndices);
    }
}

void AssimpLoader::ProcessMesh(aiMesh* mesh, const aiScene* scene,
                               std::vector<Vertex>& vertices,
                               std::vector<uint32_t>& indices) {
    uint32_t baseIndex = static_cast<uint32_t>(vertices.size());
    
    // Extract vertices
    for (uint32_t i = 0; i < mesh->mNumVertices; i++) {
        Vertex vertex;
        
        // Position
        vertex.position = {
            mesh->mVertices[i].x,
            mesh->mVertices[i].y,
            mesh->mVertices[i].z
        };
        
        // Normal
        if (mesh->HasNormals()) {
            vertex.normal = {
                mesh->mNormals[i].x,
                mesh->mNormals[i].y,
                mesh->mNormals[i].z
            };
        } else {
            vertex.normal = { 0.0f, 1.0f, 0.0f };
        }
        
        // Texture coordinates (use first UV channel)
        if (mesh->HasTextureCoords(0)) {
            vertex.texCoord = {
                mesh->mTextureCoords[0][i].x,
                mesh->mTextureCoords[0][i].y
            };
        } else {
            vertex.texCoord = { 0.0f, 0.0f };
        }
        
        // Tangent (for normal mapping)
        if (mesh->HasTangentsAndBitangents()) {
            vertex.tangent = {
                mesh->mTangents[i].x,
                mesh->mTangents[i].y,
                mesh->mTangents[i].z
            };
        } else {
            vertex.tangent = { 1.0f, 0.0f, 0.0f };
        }
        
        // Vertex color (if present)
        if (mesh->HasVertexColors(0)) {
            vertex.color = {
                mesh->mColors[0][i].r,
                mesh->mColors[0][i].g,
                mesh->mColors[0][i].b,
                mesh->mColors[0][i].a
            };
        } else {
            vertex.color = { 1.0f, 1.0f, 1.0f, 1.0f };
        }
        
        vertices.push_back(vertex);
    }
    
    // Extract indices
    for (uint32_t i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        for (uint32_t j = 0; j < face.mNumIndices; j++) {
            indices.push_back(baseIndex + face.mIndices[j]);
        }
    }
}

std::vector<std::string> AssimpLoader::ExtractTexturePaths(const aiScene* scene,
                                                           const std::string& modelDir) {
    std::vector<std::string> textures(LoadedModel::MAX_TEXTURES);
    
    if (scene->mNumMaterials == 0) {
        DebugManager::GetInstance().Log("AssimpLoader", "No materials found");
        return textures;
    }
    
    // Use first material (for multi-material models, this should be extended)
    aiMaterial* material = scene->mMaterials[0];
    
    // Helper lambda to get texture path
    auto getTexture = [&](aiTextureType type) -> std::string {
        if (material->GetTextureCount(type) > 0) {
            aiString path;
            material->GetTexture(type, 0, &path);
            std::string texPath = path.C_Str();
            
            // Handle embedded textures (marked with '*')
            if (!texPath.empty() && texPath[0] == '*') {
                // Return the embedded texture reference as-is
                // This will be handled separately via embeddedTextures
                DebugManager::GetInstance().Log("AssimpLoader", 
                    "Found embedded texture reference: " + texPath);
                return texPath;  // Return "*0", "*1", etc.
            }
            
            // Convert to absolute path for external textures
            if (texPath.find(":\\") == std::string::npos && texPath[0] != '/') {
                texPath = modelDir + texPath;
            }
            
            return texPath;
        }
        return "";
    };
    
    // Extract PBR textures - try multiple texture types for compatibility
    // Albedo/Base Color
    textures[LoadedModel::ALBEDO] = getTexture(aiTextureType_DIFFUSE);
    if (textures[LoadedModel::ALBEDO].empty()) {
        textures[LoadedModel::ALBEDO] = getTexture(aiTextureType_BASE_COLOR);
    }
    
    // Normal map
    textures[LoadedModel::NORMAL] = getTexture(aiTextureType_NORMALS);
    if (textures[LoadedModel::NORMAL].empty()) {
        textures[LoadedModel::NORMAL] = getTexture(aiTextureType_HEIGHT);
    }
    if (textures[LoadedModel::NORMAL].empty()) {
        textures[LoadedModel::NORMAL] = getTexture(aiTextureType_NORMAL_CAMERA);
    }
    
    // Metallic
    textures[LoadedModel::METALLIC] = getTexture(aiTextureType_METALNESS);
    if (textures[LoadedModel::METALLIC].empty()) {
        textures[LoadedModel::METALLIC] = getTexture(aiTextureType_REFLECTION);
    }
    
    // Roughness
    textures[LoadedModel::ROUGHNESS] = getTexture(aiTextureType_DIFFUSE_ROUGHNESS);
    if (textures[LoadedModel::ROUGHNESS].empty()) {
        textures[LoadedModel::ROUGHNESS] = getTexture(aiTextureType_SHININESS);
    }
    
    // Ambient Occlusion
    textures[LoadedModel::AO] = getTexture(aiTextureType_AMBIENT_OCCLUSION);
    if (textures[LoadedModel::AO].empty()) {
        textures[LoadedModel::AO] = getTexture(aiTextureType_LIGHTMAP);
    }
    
    // Emissive
    textures[LoadedModel::EMISSIVE] = getTexture(aiTextureType_EMISSIVE);
    if (textures[LoadedModel::EMISSIVE].empty()) {
        textures[LoadedModel::EMISSIVE] = getTexture(aiTextureType_EMISSION_COLOR);
    }
    
    // Log extracted textures
    std::string logMsg = "Extracted texture paths: ";
    const char* typeNames[] = {"Albedo", "Normal", "Metallic", "Roughness", "AO", "Emissive"};
    for (size_t i = 0; i < textures.size() && i < 6; i++) {
        if (!textures[i].empty()) {
            logMsg += typeNames[i] + std::string("=") + textures[i] + " ";
        }
    }
    DebugManager::GetInstance().Log("AssimpLoader", logMsg);
    
    return textures;
}

std::vector<EmbeddedTexture> AssimpLoader::ExtractEmbeddedTextures(
    const aiScene* scene,
    const std::string& modelDir
) {
    std::vector<EmbeddedTexture> embedded(LoadedModel::MAX_TEXTURES);
    
    if (scene->mNumTextures == 0) {
        DebugManager::GetInstance().Log("AssimpLoader", "No embedded textures found");
        return embedded;
    }
    
    std::cout << "[AssimpLoader] Extracting " << scene->mNumTextures << " embedded textures" << std::endl;
    
    // First, create a map of all embedded textures by their index
    std::vector<EmbeddedTexture> allEmbedded(scene->mNumTextures);
    
    for (unsigned int i = 0; i < scene->mNumTextures; i++) {
        aiTexture* tex = scene->mTextures[i];
        EmbeddedTexture& embTex = allEmbedded[i];
        
        if (tex->mHeight == 0) {
            // Compressed texture (PNG, JPG, etc.)
            embTex.isCompressed = true;
            embTex.width = tex->mWidth;  // mWidth is the data size in bytes for compressed
            embTex.height = 0;
            embTex.formatHint = tex->achFormatHint;  // "png", "jpg", etc.
            
            // Copy compressed data
            embTex.data.resize(tex->mWidth);
            memcpy(embTex.data.data(), tex->pcData, tex->mWidth);
            
            std::cout << "[AssimpLoader] Embedded texture " << i 
                      << ": compressed " << embTex.formatHint 
                      << ", size=" << tex->mWidth << " bytes" << std::endl;
        } else {
            // Uncompressed RGBA texture
            embTex.isCompressed = false;
            embTex.width = tex->mWidth;
            embTex.height = tex->mHeight;
            embTex.formatHint = "rgba";
            
            // Copy RGBA data (aiTexel is BGRA, need to convert)
            size_t pixelCount = tex->mWidth * tex->mHeight;
            embTex.data.resize(pixelCount * 4);
            
            for (size_t p = 0; p < pixelCount; p++) {
                embTex.data[p * 4 + 0] = tex->pcData[p].r;
                embTex.data[p * 4 + 1] = tex->pcData[p].g;
                embTex.data[p * 4 + 2] = tex->pcData[p].b;
                embTex.data[p * 4 + 3] = tex->pcData[p].a;
            }
            
            std::cout << "[AssimpLoader] Embedded texture " << i 
                      << ": uncompressed RGBA " 
                      << tex->mWidth << "x" << tex->mHeight << std::endl;
        }
    }
    
    // Now map textures to their types based on material references
    if (scene->mNumMaterials > 0) {
        aiMaterial* material = scene->mMaterials[0];
        
        auto mapEmbeddedTexture = [&](aiTextureType type, int targetSlot) {
            if (material->GetTextureCount(type) > 0) {
                aiString path;
                material->GetTexture(type, 0, &path);
                std::string texPath = path.C_Str();
                
                // Check if this is an embedded texture reference
                if (!texPath.empty() && texPath[0] == '*') {
                    int texIndex = std::stoi(texPath.substr(1));
                    if (texIndex >= 0 && texIndex < static_cast<int>(allEmbedded.size())) {
                        embedded[targetSlot] = allEmbedded[texIndex];
                        std::cout << "[AssimpLoader] Mapped embedded texture " << texIndex 
                                  << " to slot " << targetSlot << std::endl;
                    }
                }
            }
        };
        
        // Map textures to their proper slots
        mapEmbeddedTexture(aiTextureType_DIFFUSE, LoadedModel::ALBEDO);
        mapEmbeddedTexture(aiTextureType_BASE_COLOR, LoadedModel::ALBEDO);
        mapEmbeddedTexture(aiTextureType_NORMALS, LoadedModel::NORMAL);
        mapEmbeddedTexture(aiTextureType_HEIGHT, LoadedModel::NORMAL);
        mapEmbeddedTexture(aiTextureType_METALNESS, LoadedModel::METALLIC);
        mapEmbeddedTexture(aiTextureType_DIFFUSE_ROUGHNESS, LoadedModel::ROUGHNESS);
        mapEmbeddedTexture(aiTextureType_AMBIENT_OCCLUSION, LoadedModel::AO);
        mapEmbeddedTexture(aiTextureType_EMISSIVE, LoadedModel::EMISSIVE);
    }
    
    // If no material mapping worked, assign textures sequentially
    bool hasAnyMapped = false;
    for (const auto& e : embedded) {
        if (!e.data.empty()) {
            hasAnyMapped = true;
            break;
        }
    }
    
    if (!hasAnyMapped && !allEmbedded.empty()) {
        // Fallback: assign first texture as albedo
        embedded[LoadedModel::ALBEDO] = allEmbedded[0];
        std::cout << "[AssimpLoader] Fallback: assigned first embedded texture as albedo" << std::endl;
        
        // If there are more, try to assign them as normal/metallic/roughness
        if (allEmbedded.size() > 1) {
            embedded[LoadedModel::NORMAL] = allEmbedded[1];
        }
        if (allEmbedded.size() > 2) {
            embedded[LoadedModel::METALLIC] = allEmbedded[2];
        }
        if (allEmbedded.size() > 3) {
            embedded[LoadedModel::ROUGHNESS] = allEmbedded[3];
        }
    }
    
    DebugManager::GetInstance().Log("AssimpLoader", 
        "Processed " + std::to_string(scene->mNumTextures) + " embedded textures");
    
    return embedded;
}

} // namespace resources
} // namespace outer_wilds