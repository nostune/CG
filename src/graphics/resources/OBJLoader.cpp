#include "OBJLoader.h"
#include "../../core/DebugManager.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <algorithm>

namespace outer_wilds {
namespace resources {

bool OBJLoader::LoadFromFile(const std::string& filename, Mesh& mesh) {
    std::cout << "[OBJLoader] Starting load: " << filename << std::endl;
    
    std::vector<DirectX::XMFLOAT3> positions;
    std::vector<DirectX::XMFLOAT3> normals;
    std::vector<DirectX::XMFLOAT2> texCoords;
    std::vector<OBJFace> faces;
    std::unordered_map<std::string, DirectX::XMFLOAT3> materials;  // 材质名 -> Kd颜色

    if (!ParseOBJFile(filename, positions, normals, texCoords, faces, materials)) {
        std::cout << "[OBJLoader] ERROR: Failed to parse file" << std::endl;
        return false;
    }
    
    std::cout << "[OBJLoader] Parsed: " << positions.size() << " positions, " 
              << normals.size() << " normals, " 
              << texCoords.size() << " texcoords, "
              << faces.size() << " triangles" << std::endl;

    // Convert to mesh vertices and indices
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Use a map to avoid duplicate vertices
    std::unordered_map<std::string, uint32_t> vertexMap;

    for (const auto& face : faces) {
        for (int i = 0; i < 3; ++i) {
            std::string key = std::to_string(face.positionIndices[i]) + "/" +
                            std::to_string(face.texCoordIndices[i]) + "/" +
                            std::to_string(face.normalIndices[i]);

            if (vertexMap.find(key) == vertexMap.end()) {
                Vertex vertex;
                vertex.position = positions[face.positionIndices[i] - 1]; // OBJ indices are 1-based

                if (face.texCoordIndices[i] > 0 && face.texCoordIndices[i] <= texCoords.size()) {
                    vertex.texCoord = texCoords[face.texCoordIndices[i] - 1];
                } else {
                    vertex.texCoord = DirectX::XMFLOAT2(0.0f, 0.0f);
                }

                if (face.normalIndices[i] > 0 && face.normalIndices[i] <= normals.size()) {
                    vertex.normal = normals[face.normalIndices[i] - 1];
                } else {
                    vertex.normal = DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f);
                }

                vertex.tangent = DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f); // Default tangent
                
                // === 关键修复：存储材质颜色到顶点 ===
                vertex.color = DirectX::XMFLOAT4(
                    face.materialColor.x,
                    face.materialColor.y,
                    face.materialColor.z,
                    1.0f
                );

                vertexMap[key] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vertex);
            }

            indices.push_back(vertexMap[key]);
        }
    }

    mesh.SetVertices(vertices);
    mesh.SetIndices(indices);

    DebugManager::GetInstance().Log("OBJLoader", "Loaded OBJ file: " + filename);
    DebugManager::GetInstance().Log("OBJLoader", "Vertices: " + std::to_string(vertices.size()) + ", Indices: " + std::to_string(indices.size()));

    return true;
}

bool OBJLoader::ParseOBJFile(const std::string& filename,
                           std::vector<DirectX::XMFLOAT3>& positions,
                           std::vector<DirectX::XMFLOAT3>& normals,
                           std::vector<DirectX::XMFLOAT2>& texCoords,
                           std::vector<OBJFace>& faces,
                           std::unordered_map<std::string, DirectX::XMFLOAT3>& materials) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open OBJ file: " << filename << std::endl;
        return false;
    }

    // 解析MTL文件以获取材质颜色
    std::string mtlFile;
    std::string currentMaterial = "default";
    DirectX::XMFLOAT3 currentColor(1.0f, 1.0f, 1.0f);

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string type;
        iss >> type;

        if (type == "mtllib") {
            iss >> mtlFile;
            // 读取MTL文件
            std::string mtlPath = filename.substr(0, filename.find_last_of("/\\") + 1) + mtlFile;
            ParseMTLForColors(mtlPath, materials);
        }
        else if (type == "usemtl") {
            iss >> currentMaterial;
            if (materials.find(currentMaterial) != materials.end()) {
                currentColor = materials[currentMaterial];
            } else {
                currentColor = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
            }
        }
        else if (type == "v") {
            DirectX::XMFLOAT3 pos;
            iss >> pos.x >> pos.y >> pos.z;
            positions.push_back(pos);
        }
        else if (type == "vn") {
            DirectX::XMFLOAT3 normal;
            iss >> normal.x >> normal.y >> normal.z;
            normals.push_back(normal);
        }
        else if (type == "vt") {
            DirectX::XMFLOAT2 texCoord;
            iss >> texCoord.x >> texCoord.y;
            texCoords.push_back(texCoord);
        }
        else if (type == "f") {
            // 读取所有顶点（可能是三角形、四边形或多边形）
            std::vector<int> posIndices, texIndices, normIndices;
            std::string vertexStr;
            
            while (iss >> vertexStr) {
                std::replace(vertexStr.begin(), vertexStr.end(), '/', ' ');
                std::istringstream vertexIss(vertexStr);
                
                int posIdx = 0, texIdx = 0, normIdx = 0;
                vertexIss >> posIdx;
                if (vertexIss.peek() != EOF) {
                    vertexIss >> texIdx;
                }
                if (vertexIss.peek() != EOF) {
                    vertexIss >> normIdx;
                }
                
                posIndices.push_back(posIdx);
                texIndices.push_back(texIdx);
                normIndices.push_back(normIdx);
            }
            
            // 三角化：把多边形分成三角形 (fan triangulation)
            // 例如四边形 [0,1,2,3] -> 三角形 [0,1,2] 和 [0,2,3]
            for (size_t i = 1; i + 1 < posIndices.size(); ++i) {
                OBJFace face = {};
                face.materialColor = currentColor;
                
                // 第一个顶点总是 0
                face.positionIndices[0] = posIndices[0];
                face.texCoordIndices[0] = texIndices[0];
                face.normalIndices[0] = normIndices[0];
                
                // 第二个顶点是 i
                face.positionIndices[1] = posIndices[i];
                face.texCoordIndices[1] = texIndices[i];
                face.normalIndices[1] = normIndices[i];
                
                // 第三个顶点是 i+1
                face.positionIndices[2] = posIndices[i + 1];
                face.texCoordIndices[2] = texIndices[i + 1];
                face.normalIndices[2] = normIndices[i + 1];
                
                faces.push_back(face);
            }
        }
    }

    file.close();
    return true;
}

void OBJLoader::ParseMTLForColors(const std::string& mtlPath, 
                                 std::unordered_map<std::string, DirectX::XMFLOAT3>& materials) {
    std::ifstream file(mtlPath);
    if (!file.is_open()) {
        DebugManager::GetInstance().Log("OBJLoader", "MTL file not found: " + mtlPath);
        return;
    }

    std::string currentMtl;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string type;
        iss >> type;

        if (type == "newmtl") {
            iss >> currentMtl;
        }
        else if (type == "Kd" && !currentMtl.empty()) {
            DirectX::XMFLOAT3 color;
            iss >> color.x >> color.y >> color.z;
            materials[currentMtl] = color;
            DebugManager::GetInstance().Log("OBJLoader", 
                "Material " + currentMtl + " color: (" + 
                std::to_string(color.x) + ", " + 
                std::to_string(color.y) + ", " + 
                std::to_string(color.z) + ")");
        }
    }
    file.close();
}

} // namespace resources
} // namespace outer_wilds