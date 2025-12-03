#pragma once
#include "Mesh.h"
#include <string>
#include <vector>
#include <DirectXMath.h>

namespace outer_wilds {
namespace resources {

class OBJLoader {
public:
    static bool LoadFromFile(const std::string& filename, Mesh& mesh);

private:
    struct OBJVertex {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT3 normal;
        DirectX::XMFLOAT2 texCoord;
    };

    struct OBJFace {
        int positionIndices[3];
        int normalIndices[3];
        int texCoordIndices[3];
    };

    static bool ParseOBJFile(const std::string& filename,
                           std::vector<DirectX::XMFLOAT3>& positions,
                           std::vector<DirectX::XMFLOAT3>& normals,
                           std::vector<DirectX::XMFLOAT2>& texCoords,
                           std::vector<OBJFace>& faces);
};

} // namespace resources
} // namespace outer_wilds