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
    
    // GPU Texture resource handles (PBR workflow)
    void* albedoTextureSRV = nullptr;      // ID3D11ShaderResourceView* for albedo/diffuse map
    void* normalTextureSRV = nullptr;      // ID3D11ShaderResourceView* for normal map
    void* metallicTextureSRV = nullptr;    // ID3D11ShaderResourceView* for metallic map
    void* roughnessTextureSRV = nullptr;   // ID3D11ShaderResourceView* for roughness map
    
    // Deprecated: Use specific texture SRVs above instead
    void* shaderProgram = nullptr;  // Kept for backward compatibility
};

} // namespace resources
} // namespace outer_wilds
