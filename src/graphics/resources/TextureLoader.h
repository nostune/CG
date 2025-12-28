#pragma once
#include <d3d11.h>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace outer_wilds {
namespace resources {

/**
 * Texture loader utility
 * Supports common image formats: JPG, PNG, TGA, BMP, etc.
 * Enhanced for GLB embedded textures
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

    /**
     * Load texture from memory (for embedded textures in GLB/GLTF)
     * @param device D3D11 device
     * @param data Pointer to image data in memory
     * @param dataSize Size of image data in bytes
     * @param formatHint Format hint (e.g., "png", "jpg") - can be empty for auto-detection
     * @param outTexture Output texture resource view
     * @return true if successful
     */
    static bool LoadFromMemory(
        ID3D11Device* device,
        const unsigned char* data,
        size_t dataSize,
        const std::string& formatHint,
        ID3D11ShaderResourceView** outTexture
    );

    /**
     * Create texture from raw RGBA pixel data
     * @param device D3D11 device
     * @param pixels RGBA pixel data
     * @param width Texture width
     * @param height Texture height
     * @param outTexture Output texture resource view
     * @return true if successful
     */
    static bool CreateFromRGBA(
        ID3D11Device* device,
        const unsigned char* pixels,
        uint32_t width,
        uint32_t height,
        ID3D11ShaderResourceView** outTexture
    );

private:
    static bool LoadDDS(ID3D11Device* device, const std::string& filename, ID3D11ShaderResourceView** outTexture);
    static bool LoadWIC(ID3D11Device* device, const std::string& filename, ID3D11ShaderResourceView** outTexture);
    static bool LoadWICFromMemory(ID3D11Device* device, const unsigned char* data, size_t dataSize, ID3D11ShaderResourceView** outTexture);
};

} // namespace resources
} // namespace outer_wilds
