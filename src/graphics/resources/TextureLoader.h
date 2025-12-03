#pragma once
#include <d3d11.h>
#include <string>
#include <wrl/client.h>

namespace outer_wilds {
namespace resources {

/**
 * Texture loader utility
 * Supports common image formats: JPG, PNG, TGA, BMP, etc.
 */
class TextureLoader {
public:
    /**
     * Load texture from file
     * @param device D3D11 device
     * @param filename Path to texture file
     * @param outTexture Output texture resource view
     * @return true if successful
     */
    static bool LoadFromFile(
        ID3D11Device* device,
        const std::string& filename,
        ID3D11ShaderResourceView** outTexture
    );

private:
    static bool LoadDDS(ID3D11Device* device, const std::string& filename, ID3D11ShaderResourceView** outTexture);
    static bool LoadWIC(ID3D11Device* device, const std::string& filename, ID3D11ShaderResourceView** outTexture);
};

} // namespace resources
} // namespace outer_wilds
