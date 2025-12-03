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
    std::vector<DirectX::XMFLOAT3> positions;
    std::vector<DirectX::XMFLOAT3> normals;
    std::vector<DirectX::XMFLOAT2> texCoords;
    std::vector<OBJFace> faces;

    if (!ParseOBJFile(filename, positions, normals, texCoords, faces)) {
        return false;
    }

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
                           std::vector<OBJFace>& faces) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open OBJ file: " << filename << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string type;
        iss >> type;

        if (type == "v") {
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
            OBJFace face = {};
            std::string vertexStr;

            for (int i = 0; i < 3 && iss >> vertexStr; ++i) {
                std::replace(vertexStr.begin(), vertexStr.end(), '/', ' ');
                std::istringstream vertexIss(vertexStr);

                vertexIss >> face.positionIndices[i];

                if (vertexIss.peek() != EOF) {
                    vertexIss >> face.texCoordIndices[i];
                } else {
                    face.texCoordIndices[i] = 0;
                }

                if (vertexIss.peek() != EOF) {
                    vertexIss >> face.normalIndices[i];
                } else {
                    face.normalIndices[i] = 0;
                }
            }

            faces.push_back(face);
        }
    }

    file.close();
    return true;
}

} // namespace resources
} // namespace outer_wilds