#include "TerrainGenerator.h"
#include <vector>

namespace outer_wilds {
namespace resources {

void TerrainGenerator::CreateGroundPlane(Mesh& mesh, float size, int divisions) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    float halfSize = size * 0.5f;
    float step = size / divisions;

    // Create vertices
    for (int i = 0; i <= divisions; ++i) {
        for (int j = 0; j <= divisions; ++j) {
            Vertex vertex;
            vertex.position.x = -halfSize + j * step;
            vertex.position.y = 0.0f; // Ground level
            vertex.position.z = -halfSize + i * step;

            vertex.normal = DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f);
            vertex.texCoord.x = static_cast<float>(j) / divisions;
            vertex.texCoord.y = static_cast<float>(i) / divisions;
            vertex.tangent = DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f);

            vertices.push_back(vertex);
        }
    }

    // Create indices
    for (int i = 0; i < divisions; ++i) {
        for (int j = 0; j < divisions; ++j) {
            int topLeft = i * (divisions + 1) + j;
            int topRight = topLeft + 1;
            int bottomLeft = (i + 1) * (divisions + 1) + j;
            int bottomRight = bottomLeft + 1;

            // First triangle
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            // Second triangle
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    mesh.SetVertices(vertices);
    mesh.SetIndices(indices);
}

void TerrainGenerator::CreateHorizonSphere(Mesh& mesh, float radius, int stacks, int slices) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Create vertices
    for (int i = 0; i <= stacks; ++i) {
        float phi = DirectX::XM_PI * 0.5f * i / stacks; // From top to bottom
        float y = radius * cosf(phi);
        float r = radius * sinf(phi);

        for (int j = 0; j <= slices; ++j) {
            float theta = 2.0f * DirectX::XM_PI * j / slices;

            Vertex vertex;
            vertex.position.x = r * cosf(theta);
            vertex.position.y = y;
            vertex.position.z = r * sinf(theta);

            // Normal points outward
            DirectX::XMVECTOR posVec = DirectX::XMLoadFloat3(&vertex.position);
            DirectX::XMVECTOR normalVec = DirectX::XMVector3Normalize(posVec);
            DirectX::XMStoreFloat3(&vertex.normal, normalVec);

            vertex.texCoord.x = static_cast<float>(j) / slices;
            vertex.texCoord.y = static_cast<float>(i) / stacks;

            // Calculate tangent (perpendicular to normal in the theta direction)
            float thetaNext = 2.0f * DirectX::XM_PI * (j + 1) / slices;
            DirectX::XMFLOAT3 tangentPos(r * cosf(thetaNext), y, r * sinf(thetaNext));
            DirectX::XMVECTOR tangentVec = DirectX::XMVectorSubtract(
                DirectX::XMLoadFloat3(&tangentPos),
                posVec
            );
            DirectX::XMStoreFloat3(&vertex.tangent, DirectX::XMVector3Normalize(tangentVec));

            vertices.push_back(vertex);
        }
    }

    // Create indices
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            int first = i * (slices + 1) + j;
            int second = first + slices + 1;

            indices.push_back(first);
            indices.push_back(second);
            indices.push_back(first + 1);

            indices.push_back(second);
            indices.push_back(second + 1);
            indices.push_back(first + 1);
        }
    }

    mesh.SetVertices(vertices);
    mesh.SetIndices(indices);
}

} // namespace resources
} // namespace outer_wilds