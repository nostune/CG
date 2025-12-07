#pragma once
#include <string>
#include <d3d11.h>

namespace outer_wilds {
namespace resources {

class Shader {
public:
    Shader();
    ~Shader();

    bool LoadFromFile(ID3D11Device* device, const std::string& vertexPath, const std::string& pixelPath);
    void Bind(ID3D11DeviceContext* context) const;
    void Unbind(ID3D11DeviceContext* context) const;

private:
    bool LoadEmbeddedGridShader(ID3D11Device* device);
    bool LoadFromHLSLFile(ID3D11Device* device, const std::string& vsPath, const std::string& psPath);

    std::string m_VertexPath;
    std::string m_PixelPath;

    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;
    ID3D11InputLayout* inputLayout = nullptr;
};

} // namespace resources
} // namespace outer_wilds
