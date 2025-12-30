#include "AssimpLoader.h"
#include "TextureLoader.h"
#include "../../core/DebugManager.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <map>

namespace outer_wilds {
namespace resources {

bool AssimpLoader::LoadFromFile(const std::string& filePath, LoadedModel& outModel,
                                const ModelLoadOptions& options) {
    if (options.verbose) {
        std::cout << "[AssimpLoader] Starting load: " << filePath << std::endl;
    }
    
    Assimp::Importer importer;
    
    // Determine file extension for format-specific handling
    std::string ext = filePath.substr(filePath.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    bool isGLTF = (ext == "gltf" || ext == "glb");
    
    if (options.verbose) {
        std::cout << "[AssimpLoader] Calling ReadFile..." << std::endl;
    }
    
    // Import flags - 根据 fastLoad 选项调整
    // 【性能关键】CalcTangentSpace 和 GenSmoothNormals 非常耗时
    unsigned int flags = 
        aiProcess_Triangulate |              // Convert all primitives to triangles (必需)
        aiProcess_JoinIdenticalVertices |    // Optimize vertex count (快速)
        aiProcess_FlipUVs;                   // Flip UV for DirectX (所有格式都需要)
    
    if (!options.fastLoad) {
        // 完整加载：包含切线和法线生成（慢但质量高）
        flags |= aiProcess_CalcTangentSpace;   // Calculate tangents for normal mapping
        flags |= aiProcess_GenSmoothNormals;   // Generate smooth normals if missing
    }
    // fastLoad 模式：跳过切线和法线计算，依赖模型自带数据
    
    const aiScene* scene = importer.ReadFile(filePath, flags);
    
    if (options.verbose) {
        std::cout << "[AssimpLoader] ReadFile returned" << std::endl;
    }
    
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cout << "[AssimpLoader] ERROR: " << importer.GetErrorString() << std::endl;
        DebugManager::GetInstance().Log("AssimpLoader", 
            "Failed to load: " + std::string(importer.GetErrorString()));
        return false;
    }
    
    if (options.verbose) {
        std::cout << "[AssimpLoader] Scene loaded. Meshes: " << scene->mNumMeshes 
                  << ", Materials: " << scene->mNumMaterials
                  << ", Textures: " << scene->mNumTextures << std::endl;
    }
    
    // Extract directory path for texture resolution
    std::string modelDir = filePath.substr(0, filePath.find_last_of("/\\") + 1);
    
    // Check if this is FBX model that needs rotation correction
    bool needsRotation = (ext == "fbx");
    
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
    
    // Apply rotation to vertices if needed (FBX: rotate +90 degrees around X axis)
    // FBX uses Y-up with Z-forward, we need Y-up with -Z-forward
    if (needsRotation) {
        // Rotation matrix for +90 degrees around X axis:
        // Y' = Z, Z' = -Y (standing up the model, head up)
        for (auto& v : vertices) {
            float oldY = v.position.y;
            float oldZ = v.position.z;
            v.position.y = oldZ;
            v.position.z = -oldY;
            
            // Also rotate normals
            oldY = v.normal.y;
            oldZ = v.normal.z;
            v.normal.y = oldZ;
            v.normal.z = -oldY;
            
            // And tangents
            oldY = v.tangent.y;
            oldZ = v.tangent.z;
            v.tangent.y = oldZ;
            v.tangent.z = -oldY;
        }
        if (options.verbose) {
            std::cout << "[AssimpLoader] Applied +90 X rotation to FBX vertices" << std::endl;
        }
    }
    
    // Calculate bounding box (skip if not needed)
    ModelBounds& bounds = outModel.bounds;
    
    if (!options.skipBoundsCalculation) {
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
        
        if (options.verbose) {
            std::cout << "[AssimpLoader] Bounds: size=(" << bounds.size.x << ", " << bounds.size.y << ", " << bounds.size.z 
                      << "), center=(" << bounds.center.x << ", " << bounds.center.y << ", " << bounds.center.z 
                      << "), radius=" << bounds.radius << std::endl;
        }
    } else {
        // Set invalid bounds to indicate they weren't calculated
        bounds = ModelBounds{};
        if (options.verbose) {
            std::cout << "[AssimpLoader] Bounds calculation SKIPPED" << std::endl;
        }
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

std::vector<EmbeddedTexture> AssimpLoader::ExtractMaterialEmbeddedTextures(
    const aiScene* scene,
    aiMaterial* material
) {
    std::vector<EmbeddedTexture> embedded(LoadedModel::MAX_TEXTURES);
    
    if (!scene || !material || scene->mNumTextures == 0) {
        return embedded;
    }
    
    // 首先获取 DIFFUSE/BASE_COLOR 纹理的索引，用于防止 EMISSIVE 使用相同纹理
    int diffuseTextureIndex = -1;
    {
        aiString path;
        if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
            material->GetTexture(aiTextureType_DIFFUSE, 0, &path);
            std::string texPath = path.C_Str();
            if (!texPath.empty() && texPath[0] == '*') {
                diffuseTextureIndex = std::stoi(texPath.substr(1));
            }
        } else if (material->GetTextureCount(aiTextureType_BASE_COLOR) > 0) {
            material->GetTexture(aiTextureType_BASE_COLOR, 0, &path);
            std::string texPath = path.C_Str();
            if (!texPath.empty() && texPath[0] == '*') {
                diffuseTextureIndex = std::stoi(texPath.substr(1));
            }
        }
    }
    
    // Helper to extract embedded texture for a specific type
    auto extractForType = [&](aiTextureType type, int targetSlot, bool checkDiffuseConflict = false) {
        if (material->GetTextureCount(type) > 0) {
            aiString path;
            material->GetTexture(type, 0, &path);
            std::string texPath = path.C_Str();
            
            // Check if this is an embedded texture reference (*N)
            if (!texPath.empty() && texPath[0] == '*') {
                int texIndex = std::stoi(texPath.substr(1));
                
                // 【重要】如果是 EMISSIVE 且索引与 DIFFUSE 相同，跳过
                // 这是模型导出时的错误配置，不应该把漫反射纹理当作自发光
                if (checkDiffuseConflict && texIndex == diffuseTextureIndex) {
                    return;  // 跳过，不使用这个作为 emissive
                }
                
                if (texIndex >= 0 && texIndex < static_cast<int>(scene->mNumTextures)) {
                    aiTexture* tex = scene->mTextures[texIndex];
                    EmbeddedTexture& embTex = embedded[targetSlot];
                    
                    if (tex->mHeight == 0) {
                        // Compressed texture
                        embTex.isCompressed = true;
                        embTex.width = tex->mWidth;
                        embTex.height = 0;
                        embTex.formatHint = tex->achFormatHint;
                        embTex.data.resize(tex->mWidth);
                        memcpy(embTex.data.data(), tex->pcData, tex->mWidth);
                    } else {
                        // Uncompressed RGBA
                        embTex.isCompressed = false;
                        embTex.width = tex->mWidth;
                        embTex.height = tex->mHeight;
                        embTex.formatHint = "rgba";
                        size_t pixelCount = tex->mWidth * tex->mHeight;
                        embTex.data.resize(pixelCount * 4);
                        for (size_t p = 0; p < pixelCount; p++) {
                            embTex.data[p * 4 + 0] = tex->pcData[p].r;
                            embTex.data[p * 4 + 1] = tex->pcData[p].g;
                            embTex.data[p * 4 + 2] = tex->pcData[p].b;
                            embTex.data[p * 4 + 3] = tex->pcData[p].a;
                        }
                    }
                }
            }
        }
    };
    
    // Extract for each texture type
    extractForType(aiTextureType_DIFFUSE, LoadedModel::ALBEDO);
    extractForType(aiTextureType_BASE_COLOR, LoadedModel::ALBEDO);
    extractForType(aiTextureType_NORMALS, LoadedModel::NORMAL);
    extractForType(aiTextureType_HEIGHT, LoadedModel::NORMAL);
    extractForType(aiTextureType_METALNESS, LoadedModel::METALLIC);
    extractForType(aiTextureType_DIFFUSE_ROUGHNESS, LoadedModel::ROUGHNESS);
    extractForType(aiTextureType_AMBIENT_OCCLUSION, LoadedModel::AO);
    // 【关键】EMISSIVE 需要检查是否与 DIFFUSE 冲突
    extractForType(aiTextureType_EMISSIVE, LoadedModel::EMISSIVE, true);
    
    return embedded;
}

// ============================================================================
// Multi-Material Model Loading
// ============================================================================

bool AssimpLoader::LoadMultiMaterialModel(const std::string& filePath, MultiMaterialModel& outModel) {
    std::cout << "[AssimpLoader] Loading multi-material model: " << filePath << std::endl;
    
    Assimp::Importer importer;
    
    // 检测文件格式
    std::string ext = filePath.substr(filePath.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    bool isGLTF = (ext == "gltf" || ext == "glb");
    
    // Import flags - 所有格式都需要 FlipUVs 以适配 DirectX
    unsigned int flags = 
        aiProcess_Triangulate |
        aiProcess_CalcTangentSpace |
        aiProcess_GenSmoothNormals |
        aiProcess_JoinIdenticalVertices |
        aiProcess_FlipUVs;                   // 所有格式都翻转 UV for DirectX
    
    const aiScene* scene = importer.ReadFile(filePath, flags);
    
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cout << "[AssimpLoader] ERROR: " << importer.GetErrorString() << std::endl;
        return false;
    }
    
    std::cout << "[AssimpLoader] Scene loaded. Meshes: " << scene->mNumMeshes 
              << ", Materials: " << scene->mNumMaterials << std::endl;
    
    std::string modelDir = filePath.substr(0, filePath.find_last_of("/\\") + 1);
    
    // Check if this is a model that needs rotation
    // FBX: +90 X rotation (Y-up Z-forward to Y-up -Z-forward)
    // GLB/GLTF: 180 X rotation (flip upside down to correct pole orientation)
    bool needsFBXRotation = (ext == "fbx");
    bool needsGLBRotation = isGLTF;  // GLB/GLTF 地球模型北极在下方，需要翻转
    
    // Group meshes by material index
    std::map<int, std::pair<std::vector<Vertex>, std::vector<uint32_t>>> materialMeshes;
    std::vector<Vertex> collisionVertices;
    std::vector<uint32_t> collisionIndices;
    
    ProcessNodeMultiMaterial(scene->mRootNode, scene, materialMeshes, collisionVertices, collisionIndices);
    
    // Apply rotation to vertices if needed
    if (needsFBXRotation) {
        // FBX: Rotate +90 degrees around X axis
        // Y' = Z, Z' = -Y (standing up the model, head up)
        for (auto& [materialIndex, meshData] : materialMeshes) {
            auto& [vertices, indices] = meshData;
            for (auto& v : vertices) {
                float oldY = v.position.y;
                float oldZ = v.position.z;
                v.position.y = oldZ;
                v.position.z = -oldY;
                
                // Also rotate normals
                oldY = v.normal.y;
                oldZ = v.normal.z;
                v.normal.y = oldZ;
                v.normal.z = -oldY;
                
                // And tangents
                oldY = v.tangent.y;
                oldZ = v.tangent.z;
                v.tangent.y = oldZ;
                v.tangent.z = -oldY;
            }
        }
        std::cout << "[AssimpLoader] Applied +90 X rotation to FBX vertices" << std::endl;
    }
    else if (needsGLBRotation) {
        // GLB/GLTF: Rotate 180 degrees around X axis (flip Y and Z)
        // This flips the model upside down: Y' = -Y, Z' = -Z
        for (auto& [materialIndex, meshData] : materialMeshes) {
            auto& [vertices, indices] = meshData;
            for (auto& v : vertices) {
                v.position.y = -v.position.y;
                v.position.z = -v.position.z;
                
                // Also rotate normals
                v.normal.y = -v.normal.y;
                v.normal.z = -v.normal.z;
                
                // And tangents
                v.tangent.y = -v.tangent.y;
                v.tangent.z = -v.tangent.z;
            }
        }
        std::cout << "[AssimpLoader] Applied 180 X rotation to GLB/GLTF vertices (pole correction)" << std::endl;
    }
    
    // ========================================
    // 【重要】自动居中模型 - 将几何中心平移到原点
    // 这对于球形行星模型非常重要，确保球心在原点
    // ========================================
    {
        // 首先计算所有顶点的边界框
        DirectX::XMFLOAT3 tempMin = { FLT_MAX, FLT_MAX, FLT_MAX };
        DirectX::XMFLOAT3 tempMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
        
        for (auto& [materialIndex, meshData] : materialMeshes) {
            auto& [vertices, indices] = meshData;
            for (const auto& v : vertices) {
                tempMin.x = (std::min)(tempMin.x, v.position.x);
                tempMin.y = (std::min)(tempMin.y, v.position.y);
                tempMin.z = (std::min)(tempMin.z, v.position.z);
                tempMax.x = (std::max)(tempMax.x, v.position.x);
                tempMax.y = (std::max)(tempMax.y, v.position.y);
                tempMax.z = (std::max)(tempMax.z, v.position.z);
            }
        }
        
        // 计算几何中心
        DirectX::XMFLOAT3 center = {
            (tempMin.x + tempMax.x) * 0.5f,
            (tempMin.y + tempMax.y) * 0.5f,
            (tempMin.z + tempMax.z) * 0.5f
        };
        
        // 只有当偏移足够大时才需要平移（避免浮点误差）
        float offsetMagnitude = std::sqrt(center.x * center.x + center.y * center.y + center.z * center.z);
        if (offsetMagnitude > 0.1f) {
            std::cout << "[AssimpLoader] Model center offset detected: (" 
                      << center.x << ", " << center.y << ", " << center.z 
                      << "), magnitude=" << offsetMagnitude << std::endl;
            
            // 平移所有顶点，使中心对齐到原点
            for (auto& [materialIndex, meshData] : materialMeshes) {
                auto& [vertices, indices] = meshData;
                for (auto& v : vertices) {
                    v.position.x -= center.x;
                    v.position.y -= center.y;
                    v.position.z -= center.z;
                }
            }
            
            std::cout << "[AssimpLoader] Applied center offset correction, model now centered at origin" << std::endl;
        }
    }
    
    // Calculate overall bounding box
    ModelBounds& bounds = outModel.bounds;
    bounds.min = { FLT_MAX, FLT_MAX, FLT_MAX };
    bounds.max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    
    // Create submesh for each material
    for (auto& [materialIndex, meshData] : materialMeshes) {
        auto& [vertices, indices] = meshData;
        
        if (vertices.empty()) continue;
        
        SubMesh subMesh;
        subMesh.materialIndex = materialIndex;
        subMesh.mesh = std::make_shared<Mesh>();
        subMesh.mesh->SetVertices(vertices);
        subMesh.mesh->SetIndices(indices);
        
        // Get material name and textures
        if (materialIndex >= 0 && materialIndex < static_cast<int>(scene->mNumMaterials)) {
            aiMaterial* material = scene->mMaterials[materialIndex];
            aiString name;
            material->Get(AI_MATKEY_NAME, name);
            subMesh.materialName = name.C_Str();
            
            // 打印材质的所有纹理类型数量
            std::cout << "[AssimpLoader] Material " << materialIndex << " (" << subMesh.materialName << "):" << std::endl;
            std::cout << "  - DIFFUSE textures: " << material->GetTextureCount(aiTextureType_DIFFUSE) << std::endl;
            std::cout << "  - BASE_COLOR textures: " << material->GetTextureCount(aiTextureType_BASE_COLOR) << std::endl;
            std::cout << "  - NORMALS textures: " << material->GetTextureCount(aiTextureType_NORMALS) << std::endl;
            std::cout << "  - EMISSIVE textures: " << material->GetTextureCount(aiTextureType_EMISSIVE) << std::endl;
            std::cout << "  - UNKNOWN textures: " << material->GetTextureCount(aiTextureType_UNKNOWN) << std::endl;
            
            // 打印每种类型的纹理路径
            for (int t = 0; t <= aiTextureType_UNKNOWN; t++) {
                aiTextureType type = static_cast<aiTextureType>(t);
                unsigned int count = material->GetTextureCount(type);
                for (unsigned int i = 0; i < count; i++) {
                    aiString path;
                    material->GetTexture(type, i, &path);
                    std::cout << "  - Type " << t << " texture " << i << ": " << path.C_Str() << std::endl;
                }
            }
            
            // Extract texture paths for this material
            subMesh.texturePaths = ExtractMaterialTexturePaths(material, modelDir);
            
            // Extract embedded textures for this material (GLB/GLTF)
            subMesh.embeddedTextures = ExtractMaterialEmbeddedTextures(scene, material);
            
            std::cout << "[AssimpLoader] SubMesh " << subMesh.materialName 
                      << ": " << vertices.size() << " vertices, "
                      << indices.size() / 3 << " triangles" << std::endl;
            
            if (!subMesh.embeddedTextures.empty() && !subMesh.embeddedTextures[0].data.empty()) {
                std::cout << "[AssimpLoader]   Has embedded albedo texture (size=" 
                          << subMesh.embeddedTextures[0].data.size() << ")" << std::endl;
            } else if (!subMesh.texturePaths.empty() && !subMesh.texturePaths[0].empty()) {
                std::cout << "[AssimpLoader]   Albedo texture: " << subMesh.texturePaths[0] << std::endl;
            } else {
                std::cout << "[AssimpLoader]   NO albedo texture found!" << std::endl;
            }
        }
        
        // Update bounding box
        for (const auto& vertex : vertices) {
            bounds.min.x = (std::min)(bounds.min.x, vertex.position.x);
            bounds.min.y = (std::min)(bounds.min.y, vertex.position.y);
            bounds.min.z = (std::min)(bounds.min.z, vertex.position.z);
            bounds.max.x = (std::max)(bounds.max.x, vertex.position.x);
            bounds.max.y = (std::max)(bounds.max.y, vertex.position.y);
            bounds.max.z = (std::max)(bounds.max.z, vertex.position.z);
        }
        
        outModel.subMeshes.push_back(std::move(subMesh));
    }
    
    // Calculate bounds center and size
    bounds.size.x = bounds.max.x - bounds.min.x;
    bounds.size.y = bounds.max.y - bounds.min.y;
    bounds.size.z = bounds.max.z - bounds.min.z;
    bounds.center.x = (bounds.min.x + bounds.max.x) * 0.5f;
    bounds.center.y = (bounds.min.y + bounds.max.y) * 0.5f;
    bounds.center.z = (bounds.min.z + bounds.max.z) * 0.5f;
    bounds.radius = std::sqrt(bounds.size.x * bounds.size.x + 
                              bounds.size.y * bounds.size.y + 
                              bounds.size.z * bounds.size.z) * 0.5f;
    
    // Store collision mesh
    if (!collisionVertices.empty()) {
        outModel.collisionVertices = collisionVertices;
        outModel.collisionIndices = collisionIndices;
        outModel.hasCollisionMesh = true;
    }
    
    std::cout << "[AssimpLoader] Loaded " << outModel.subMeshes.size() << " sub-meshes" << std::endl;
    
    return !outModel.subMeshes.empty();
}

void AssimpLoader::ProcessNodeMultiMaterial(
    aiNode* node, const aiScene* scene,
    std::map<int, std::pair<std::vector<Vertex>, std::vector<uint32_t>>>& materialMeshes,
    std::vector<Vertex>& collisionVertices,
    std::vector<uint32_t>& collisionIndices
) {
    for (uint32_t i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        
        std::string meshName = mesh->mName.C_Str();
        std::transform(meshName.begin(), meshName.end(), meshName.begin(), ::tolower);
        
        if (meshName.find("collision") != std::string::npos || 
            meshName.find("collider") != std::string::npos) {
            ProcessMesh(mesh, scene, collisionVertices, collisionIndices);
        } else {
            // Group by material index
            int matIndex = mesh->mMaterialIndex;
            auto& [vertices, indices] = materialMeshes[matIndex];
            ProcessMesh(mesh, scene, vertices, indices);
        }
    }
    
    for (uint32_t i = 0; i < node->mNumChildren; i++) {
        ProcessNodeMultiMaterial(node->mChildren[i], scene, materialMeshes, collisionVertices, collisionIndices);
    }
}

std::vector<std::string> AssimpLoader::ExtractMaterialTexturePaths(aiMaterial* material, const std::string& modelDir) {
    std::vector<std::string> textures(MultiMaterialModel::MAX_TEXTURES);
    
    auto getTexture = [&](aiTextureType type) -> std::string {
        if (material->GetTextureCount(type) > 0) {
            aiString path;
            material->GetTexture(type, 0, &path);
            std::string texPath = path.C_Str();
            
            // Skip embedded texture references
            if (!texPath.empty() && texPath[0] == '*') {
                return texPath;
            }
            
            // Convert to absolute path
            if (texPath.find(":\\") == std::string::npos && texPath[0] != '/') {
                texPath = modelDir + texPath;
            }
            
            return texPath;
        }
        return "";
    };
    
    // Albedo
    textures[MultiMaterialModel::ALBEDO] = getTexture(aiTextureType_DIFFUSE);
    if (textures[MultiMaterialModel::ALBEDO].empty()) {
        textures[MultiMaterialModel::ALBEDO] = getTexture(aiTextureType_BASE_COLOR);
    }
    
    // Normal
    textures[MultiMaterialModel::NORMAL] = getTexture(aiTextureType_NORMALS);
    if (textures[MultiMaterialModel::NORMAL].empty()) {
        textures[MultiMaterialModel::NORMAL] = getTexture(aiTextureType_HEIGHT);
    }
    
    // Metallic
    textures[MultiMaterialModel::METALLIC] = getTexture(aiTextureType_METALNESS);
    
    // Roughness
    textures[MultiMaterialModel::ROUGHNESS] = getTexture(aiTextureType_DIFFUSE_ROUGHNESS);
    
    // AO
    textures[MultiMaterialModel::AO] = getTexture(aiTextureType_AMBIENT_OCCLUSION);
    
    // Emissive
    textures[MultiMaterialModel::EMISSIVE] = getTexture(aiTextureType_EMISSIVE);
    
    return textures;
}

} // namespace resources
} // namespace outer_wilds