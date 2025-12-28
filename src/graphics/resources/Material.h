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
    DirectX::XMFLOAT3 emissiveColor = { 0.0f, 0.0f, 0.0f };  // Emissive/自发光颜色
    float emissiveStrength = 1.0f;  // 发光强度
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    
    std::string albedoTexture;
    std::string normalTexture;
    std::string metallicTexture;
    std::string roughnessTexture;
    std::string emissiveTexture;  // Emissive 纹理路径
    
    bool isTransparent = false;
    bool isEmissive = false;  // 是否是发光材质
    
    // GPU Texture resource handles (PBR workflow)
    void* albedoTextureSRV = nullptr;      // ID3D11ShaderResourceView* for albedo/diffuse map
    void* normalTextureSRV = nullptr;      // ID3D11ShaderResourceView* for normal map
    void* metallicTextureSRV = nullptr;    // ID3D11ShaderResourceView* for metallic map
    void* roughnessTextureSRV = nullptr;   // ID3D11ShaderResourceView* for roughness map
    void* emissiveTextureSRV = nullptr;    // ID3D11ShaderResourceView* for emissive map
    
    // Deprecated: Use specific texture SRVs above instead
    void* shaderProgram = nullptr;  // Kept for backward compatibility
};

} // namespace resources
} // namespace outer_wilds
