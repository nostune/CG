#include "Shader.h"
#include "../../core/DebugManager.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <iostream>
#include <fstream>
#include <vector>

namespace outer_wilds {
namespace resources {

Shader::Shader() = default;

Shader::~Shader() {
    if (inputLayout) inputLayout->Release();
    if (pixelShader) pixelShader->Release();
    if (vertexShader) vertexShader->Release();
}

bool Shader::LoadFromFile(ID3D11Device* device, const std::string& vertexPath, const std::string& pixelPath) {
    return LoadFromFile(device, vertexPath, pixelPath, false);
}

bool Shader::LoadFromFile(ID3D11Device* device, const std::string& vertexPath, const std::string& pixelPath, bool positionOnly) {
    m_VertexPath = vertexPath;
    m_PixelPath = pixelPath;

    // ============================================
    // IMPORTANT: Shader Loading Strategy
    // ============================================
    // - "grid.vs" + "grid.ps" → Use embedded grid shader (no texture, procedural grid)
    // - "textured.vs" + "textured.ps" → Load from shaders/textured.hlsl (supports textures)
    // - Other names → Load from corresponding .hlsl file
    // 
    // WHY: Grid shader is hardcoded for performance, textured shader reads from file for flexibility
    // ============================================
    
    bool useEmbedded = (vertexPath == "grid.vs" && pixelPath == "grid.ps");
    
    if (useEmbedded) {
        // Use embedded grid shader for ground plane
        return LoadEmbeddedGridShader(device);
    } else {
        // Load from HLSL file (e.g., shaders/textured.hlsl)
        return LoadFromHLSLFile(device, vertexPath, pixelPath, positionOnly);
    }
}

bool Shader::LoadEmbeddedGridShader(ID3D11Device* device) {
    // For now, we'll use embedded shader code for grid ground
    const char* vertexShaderCode = R"(
        cbuffer PerFrame : register(b0)
        {
            matrix viewProjection;
        };

        cbuffer PerObject : register(b1)
        {
            matrix world;
        };

        struct VS_INPUT
        {
            float3 position : POSITION;
            float3 normal : NORMAL;
            float2 texCoord : TEXCOORD;
        };

        struct VS_OUTPUT
        {
            float4 position : SV_POSITION;
            float3 normal : NORMAL;
            float2 texCoord : TEXCOORD;
            float3 worldPos : WORLDPOS;
        };

        VS_OUTPUT main(VS_INPUT input)
        {
            VS_OUTPUT output;
            
            // Transform position to world space
            float4 worldPos = mul(float4(input.position, 1.0f), world);
            
            // Transform to clip space
            output.position = mul(worldPos, viewProjection);
            
            // Transform normal to world space
            output.normal = mul(input.normal, (float3x3)world);
            
            // Pass through texture coordinates
            output.texCoord = input.texCoord;
            
            // Store world position for grid calculation
            output.worldPos = worldPos.xyz;
            
            return output;
        }
    )";

    const char* pixelShaderCode = R"(
        struct PS_INPUT
        {
            float4 position : SV_POSITION;
            float3 normal : NORMAL;
            float2 texCoord : TEXCOORD;
            float3 worldPos : WORLDPOS;
        };

        float4 main(PS_INPUT input) : SV_TARGET
        {
            // Grid parameters
            float gridSize = 1.0f; // 1 unit grid
            float lineWidth = 0.05f; // Line width in world units
            
            // Calculate grid lines
            float2 gridPos = frac(input.worldPos.xz / gridSize);
            float2 distToLine = min(gridPos, 1.0 - gridPos);
            float gridMask = 1.0 - smoothstep(0.0, lineWidth / gridSize, min(distToLine.x, distToLine.y));
            
            // Lighting
            float3 lightDir = normalize(float3(0.5f, 1.0f, -0.5f));
            float3 normal = normalize(input.normal);
            float diffuse = max(dot(normal, lightDir), 0.0f);
            float ambient = 0.3f;
            
            // Base color (dark gray)
            float3 baseColor = float3(0.2f, 0.2f, 0.2f);
            
            // Grid color (lighter gray)
            float3 gridColor = float3(0.6f, 0.6f, 0.6f);
            
            // Mix colors based on grid mask
            float3 color = lerp(baseColor, gridColor, gridMask);
            
            // Apply lighting
            float3 finalColor = color * (ambient + diffuse);
            
            return float4(finalColor, 1.0f);
        }
    )";

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = D3DCompile(vertexShaderCode, strlen(vertexShaderCode), nullptr, nullptr, nullptr,
        "main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) { 
            DebugManager::GetInstance().Log("Shader", "Vertex shader compilation failed: " + std::string((char*)errorBlob->GetBufferPointer()));
            errorBlob->Release(); 
        } else {
            DebugManager::GetInstance().Log("Shader", "Vertex shader compilation failed with HRESULT: " + std::to_string(hr));
        }
        return false;
    }

    hr = D3DCompile(pixelShaderCode, strlen(pixelShaderCode), nullptr, nullptr, nullptr,
        "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) { 
            DebugManager::GetInstance().Log("Shader", "Pixel shader compilation failed: " + std::string((char*)errorBlob->GetBufferPointer()));
            errorBlob->Release(); 
        } else {
            DebugManager::GetInstance().Log("Shader", "Pixel shader compilation failed with HRESULT: " + std::to_string(hr));
        }
        if (vsBlob) vsBlob->Release();
        return false;
    }

    if (vsBlob && psBlob) {
        device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vertexShader);
        device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pixelShader);

        // Create input layout
        D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        device->CreateInputLayout(layoutDesc, ARRAYSIZE(layoutDesc), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout);
    }

    if (vsBlob) vsBlob->Release();
    if (psBlob) psBlob->Release();
    if (errorBlob) errorBlob->Release();

    DebugManager::GetInstance().Log("Shader", "Grid ground shader loaded successfully");
    return true;
}

bool Shader::LoadFromHLSLFile(ID3D11Device* device, const std::string& vsPath, const std::string& psPath, bool positionOnly) {
    // ============================================
    // Construct HLSL file path from shader name
    // ============================================
    // Example: "textured.vs" → "shaders/textured.hlsl"
    // Both vertex and pixel shaders are in the same .hlsl file
    // ============================================
    
    std::string baseName = vsPath;
    // Remove .vs or .ps extension, expect .hlsl
    size_t dotPos = baseName.find_last_of('.');
    if (dotPos != std::string::npos) {
        baseName = baseName.substr(0, dotPos);
    }
    
    // 尝试多个可能的路径
    std::vector<std::string> searchPaths = {
        "shaders/" + baseName + ".hlsl",
        "../shaders/" + baseName + ".hlsl",
        "../../shaders/" + baseName + ".hlsl",
        "C:/Users/kkakk/homework/OuterWilds/shaders/" + baseName + ".hlsl"  // 绝对路径兜底
    };
    
    std::string hlslFile;
    std::ifstream file;
    
    for (const auto& path : searchPaths) {
        file.open(path);
        if (file.is_open()) {
            hlslFile = path;
            break;
        }
    }
    
    if (!file.is_open()) {
        DebugManager::GetInstance().Log("Shader", "Failed to open HLSL file. Tried paths:");
        for (const auto& path : searchPaths) {
            DebugManager::GetInstance().Log("Shader", "  - " + path);
        }
        return false;
    }
    
    std::string hlslCode((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    
    if (hlslCode.empty()) {
        DebugManager::GetInstance().Log("Shader", "HLSL file is empty: " + hlslFile);
        return false;
    }
    
    DebugManager::GetInstance().Log("Shader", "Loaded HLSL file: " + hlslFile + " (" + std::to_string(hlslCode.size()) + " bytes)");
    
    // Compile vertex shader
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    
    HRESULT hr = D3DCompile(
        hlslCode.c_str(),
        hlslCode.size(),
        hlslFile.c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VSMain",
        "vs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        &vsBlob,
        &errorBlob
    );
    
    if (FAILED(hr)) {
        if (errorBlob) {
            DebugManager::GetInstance().Log("Shader", "Vertex shader compilation failed: " + 
                std::string((char*)errorBlob->GetBufferPointer()));
            errorBlob->Release();
        }
        DebugManager::GetInstance().Log("Shader", "Failed to compile vertex shader from: " + hlslFile);
        return false;
    }
    
    // Compile pixel shader
    hr = D3DCompile(
        hlslCode.c_str(),
        hlslCode.size(),
        hlslFile.c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PSMain",
        "ps_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        &psBlob,
        &errorBlob
    );
    
    if (FAILED(hr)) {
        if (errorBlob) {
            DebugManager::GetInstance().Log("Shader", "Pixel shader compilation failed: " + 
                std::string((char*)errorBlob->GetBufferPointer()));
            errorBlob->Release();
        }
        if (vsBlob) vsBlob->Release();
        DebugManager::GetInstance().Log("Shader", "Failed to compile pixel shader from: " + hlslFile);
        return false;
    }
    
    // Create shader objects
    hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vertexShader);
    if (FAILED(hr)) {
        DebugManager::GetInstance().Log("Shader", "Failed to create vertex shader");
        vsBlob->Release();
        psBlob->Release();
        return false;
    }
    
    hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pixelShader);
    if (FAILED(hr)) {
        DebugManager::GetInstance().Log("Shader", "Failed to create pixel shader");
        vsBlob->Release();
        psBlob->Release();
        return false;
    }
    
    // Create input layout
    HRESULT hrLayout;
    if (positionOnly) {
        // 简化的顶点格式（仅位置 - 用于skybox等）
        D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        
        hrLayout = device->CreateInputLayout(
            layoutDesc,
            ARRAYSIZE(layoutDesc),
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            &inputLayout
        );
    } else {
        // 完整的顶点格式
        D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        
        hrLayout = device->CreateInputLayout(
            layoutDesc,
            ARRAYSIZE(layoutDesc),
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            &inputLayout
        );
    }
    
    vsBlob->Release();
    psBlob->Release();
    
    if (FAILED(hrLayout)) {
        DebugManager::GetInstance().Log("Shader", "Failed to create input layout");
        return false;
    }
    
    DebugManager::GetInstance().Log("Shader", "✅ Successfully loaded shader from: " + hlslFile);
    return true;
}

void Shader::Bind(ID3D11DeviceContext* context) const {
    if (context) {
        context->IASetInputLayout(inputLayout);
        context->VSSetShader(vertexShader, nullptr, 0);
        context->PSSetShader(pixelShader, nullptr, 0);
    }
}

void Shader::Unbind(ID3D11DeviceContext* context) const {
    if (context) {
        context->VSSetShader(nullptr, nullptr, 0);
        context->PSSetShader(nullptr, nullptr, 0);
    }
}

} // namespace resources
} // namespace outer_wilds