#include "AssimpLoader.h"
#include "../../core/DebugManager.h"
#include <algorithm>

namespace outer_wilds {
namespace resources {

bool AssimpLoader::LoadFromFile(const std::string& filePath, LoadedModel& outModel) {
    std::cout << "[AssimpLoader] Starting load: " << filePath << std::endl;
    
    Assimp::Importer importer;
    
    std::cout << "[AssimpLoader] Calling ReadFile with minimal flags..." << std::endl;
    
    // Import with MINIMAL post-processing flags to avoid hanging
    const aiScene* scene = importer.ReadFile(filePath,
        aiProcess_Triangulate |           // Convert all primitives to triangles
        aiProcess_FlipUVs                 // Flip UV coordinates for DirectX
    );
    
    std::cout << "[AssimpLoader] ReadFile returned" << std::endl;
    
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cout << "[AssimpLoader] ERROR: " << importer.GetErrorString() << std::endl;
        DebugManager::GetInstance().Log("AssimpLoader", 
            "Failed to load: " + std::string(importer.GetErrorString()));
        return false;
    }
    
    std::cout << "[AssimpLoader] Scene loaded. Meshes: " << scene->mNumMeshes << std::endl;
    
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
    
    // Extract texture paths
    outModel.texturePaths = ExtractTexturePaths(scene, modelDir);
    
    DebugManager::GetInstance().Log("AssimpLoader", 
        "Loaded: " + std::to_string(vertices.size()) + " vertices, " +
        std::to_string(indices.size() / 3) + " triangles, " +
        std::to_string(outModel.texturePaths.size()) + " textures");
    
    return true;
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
    std::vector<std::string> textures(4); // [albedo, normal, metallic, roughness]
    
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
            
            // Handle embedded textures (FBX often has these)
            if (texPath[0] == '*') {
                DebugManager::GetInstance().Log("AssimpLoader", 
                    "Warning: Embedded texture not supported: " + texPath);
                return "";
            }
            
            // Convert to absolute path
            if (texPath.find(":\\") == std::string::npos && texPath[0] != '/') {
                texPath = modelDir + texPath;
            }
            
            return texPath;
        }
        return "";
    };
    
    // Extract PBR textures
    textures[LoadedModel::ALBEDO] = getTexture(aiTextureType_DIFFUSE);
    if (textures[LoadedModel::ALBEDO].empty()) {
        textures[LoadedModel::ALBEDO] = getTexture(aiTextureType_BASE_COLOR);
    }
    
    textures[LoadedModel::NORMAL] = getTexture(aiTextureType_NORMALS);
    if (textures[LoadedModel::NORMAL].empty()) {
        textures[LoadedModel::NORMAL] = getTexture(aiTextureType_HEIGHT); // Bump map fallback
    }
    
    textures[LoadedModel::METALLIC] = getTexture(aiTextureType_METALNESS);
    textures[LoadedModel::ROUGHNESS] = getTexture(aiTextureType_DIFFUSE_ROUGHNESS);
    
    // Log extracted textures
    std::string logMsg = "Extracted textures: ";
    for (size_t i = 0; i < textures.size(); i++) {
        if (!textures[i].empty()) {
            const char* types[] = {"Albedo", "Normal", "Metallic", "Roughness"};
            logMsg += types[i] + std::string("=") + textures[i] + " ";
        }
    }
    DebugManager::GetInstance().Log("AssimpLoader", logMsg);
    
    return textures;
}

} // namespace resources
} // namespace outer_wilds
