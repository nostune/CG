#pragma once
#include <DirectXMath.h>
#include <string>

namespace outer_wilds {
namespace resources {

class Material {
public:
    Material() = default;
    ~Material() = default;

    DirectX::XMFLOAT4 albedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    
    std::string albedoTexture;
    std::string normalTexture;
    std::string metallicTexture;
    std::string roughnessTexture;
    
    bool isTransparent = false;
    
    // Shader resource handles (to be filled by renderer)
    void* shaderProgram = nullptr;
};

} // namespace resources
} // namespace outer_wilds
