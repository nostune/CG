#include "TextureLoader.h"
#include "../../core/DebugManager.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <fstream>

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

namespace outer_wilds {
namespace resources {

bool TextureLoader::LoadFromFile(
    ID3D11Device* device,
    const std::string& filename,
    ID3D11ShaderResourceView** outTexture
) {
    if (!device || !outTexture) {
        DebugManager::GetInstance().Log("TextureLoader", "Invalid parameters");
        return false;
    }

    // Check file extension
    std::string ext = filename.substr(filename.find_last_of('.') + 1);
    if (ext == "dds" || ext == "DDS") {
        return LoadDDS(device, filename, outTexture);
    } else {
        return LoadWIC(device, filename, outTexture);
    }
}

bool TextureLoader::LoadDDS(ID3D11Device* device, const std::string& filename, ID3D11ShaderResourceView** outTexture) {
    // DDS loading would require DirectXTex library
    // For simplicity, we'll skip DDS support for now
    DebugManager::GetInstance().Log("TextureLoader", "DDS format not yet implemented");
    return false;
}

bool TextureLoader::LoadWIC(ID3D11Device* device, const std::string& filename, ID3D11ShaderResourceView** outTexture) {
    // Convert filename to wide string
    std::wstring wFilename(filename.begin(), filename.end());

    // Initialize COM
    CoInitialize(nullptr);

    ComPtr<IWICImagingFactory> wicFactory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&wicFactory)
    );

    if (FAILED(hr)) {
        DebugManager::GetInstance().Log("TextureLoader", "Failed to create WIC factory");
        return false;
    }

    // Load image from file
    ComPtr<IWICBitmapDecoder> decoder;
    hr = wicFactory->CreateDecoderFromFilename(
        wFilename.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        &decoder
    );

    if (FAILED(hr)) {
        DebugManager::GetInstance().Log("TextureLoader", "Failed to create decoder for: " + filename);
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        DebugManager::GetInstance().Log("TextureLoader", "Failed to get frame");
        return false;
    }

    // Convert to RGBA format
    ComPtr<IWICFormatConverter> converter;
    hr = wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr)) {
        DebugManager::GetInstance().Log("TextureLoader", "Failed to create format converter");
        return false;
    }

    hr = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom
    );

    if (FAILED(hr)) {
        DebugManager::GetInstance().Log("TextureLoader", "Failed to initialize converter");
        return false;
    }

    // Get image dimensions
    UINT width, height;
    hr = converter->GetSize(&width, &height);
    if (FAILED(hr)) {
        DebugManager::GetInstance().Log("TextureLoader", "Failed to get image size");
        return false;
    }

    // Copy pixel data
    UINT stride = width * 4;
    UINT imageSize = stride * height;
    std::vector<BYTE> pixels(imageSize);

    hr = converter->CopyPixels(
        nullptr,
        stride,
        imageSize,
        pixels.data()
    );

    if (FAILED(hr)) {
        DebugManager::GetInstance().Log("TextureLoader", "Failed to copy pixels");
        return false;
    }

    // Create D3D11 texture
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;
    texDesc.MiscFlags = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = pixels.data();
    initData.SysMemPitch = stride;
    initData.SysMemSlicePitch = imageSize;

    ComPtr<ID3D11Texture2D> texture;
    hr = device->CreateTexture2D(&texDesc, &initData, &texture);
    if (FAILED(hr)) {
        DebugManager::GetInstance().Log("TextureLoader", "Failed to create texture");
        return false;
    }

    // Create shader resource view
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = device->CreateShaderResourceView(texture.Get(), &srvDesc, outTexture);
    if (FAILED(hr)) {
        DebugManager::GetInstance().Log("TextureLoader", "Failed to create SRV");
        return false;
    }

    DebugManager::GetInstance().Log("TextureLoader", "Successfully loaded texture: " + filename + 
                                   " (" + std::to_string(width) + "x" + std::to_string(height) + ")");

    return true;
}

} // namespace resources
} // namespace outer_wilds
