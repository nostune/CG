// PBR Textured shader with multi-texture support

cbuffer PerFrameBuffer : register(b0)
{
    matrix viewProjection;
    float3 cameraPosition;
    float padding1;
};

cbuffer PerObjectBuffer : register(b1)
{
    matrix world;
    float4 color;
};

// Multi-texture PBR maps
Texture2D albedoTexture : register(t0);    // Diffuse/Albedo map
Texture2D normalTexture : register(t1);    // Normal map (tangent space)
Texture2D metallicTexture : register(t2);  // Metallic map
Texture2D roughnessTexture : register(t3); // Roughness map
SamplerState textureSampler : register(s0);

struct VS_INPUT
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float3 tangent : TANGENT;  // For normal mapping
    float4 color : COLOR0;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float3 tangent : TANGENT;
    float4 color : COLOR0;
};

// Vertex Shader
PS_INPUT VSMain(VS_INPUT input)
{
    PS_INPUT output;
    
    // Transform to world space
    float4 worldPos = mul(float4(input.position, 1.0f), world);
    output.worldPos = worldPos.xyz;
    
    // Transform to clip space
    output.position = mul(worldPos, viewProjection);
    
    // Transform normal and tangent to world space
    output.normal = mul(input.normal, (float3x3)world);
    output.normal = normalize(output.normal);
    
    output.tangent = mul(input.tangent, (float3x3)world);
    output.tangent = normalize(output.tangent);
    
    output.texcoord = input.texcoord;
    output.color = input.color;
    
    return output;
}

// Pixel Shader with PBR multi-texture support
float4 PSMain(PS_INPUT input) : SV_TARGET
{
    // Sample albedo texture (or use vertex color as fallback)
    float4 albedo = albedoTexture.Sample(textureSampler, input.texcoord);
    bool hasVertexColor = (input.color.r != 1.0f || input.color.g != 1.0f || input.color.b != 1.0f);
    if (hasVertexColor && albedo.r > 0.99f && albedo.g > 0.99f && albedo.b > 0.99f) {
        albedo = input.color;
    }
    
    // Sample normal map (if available) and apply tangent-space normal mapping
    float3 normal = normalize(input.normal);
    float3 normalMap = normalTexture.Sample(textureSampler, input.texcoord).rgb;
    
    // Check if we have a valid normal map (not default [0.5, 0.5, 1.0])
    bool hasNormalMap = (abs(normalMap.r - 0.5f) > 0.01f || abs(normalMap.g - 0.5f) > 0.01f);
    if (hasNormalMap) {
        // Convert from [0,1] to [-1,1]
        normalMap = normalMap * 2.0f - 1.0f;
        
        // Build TBN matrix for tangent-space to world-space transformation
        float3 T = normalize(input.tangent);
        float3 N = normalize(input.normal);
        T = normalize(T - dot(T, N) * N);  // Gram-Schmidt orthogonalize
        float3 B = cross(N, T);
        float3x3 TBN = float3x3(T, B, N);
        
        normal = normalize(mul(normalMap, TBN));
    }
    
    // Sample PBR maps (with fallback to default values)
    float metallic = metallicTexture.Sample(textureSampler, input.texcoord).r;
    float roughness = roughnessTexture.Sample(textureSampler, input.texcoord).r;
    
    // Simple PBR-inspired lighting (directional + ambient)
    float3 lightDir = normalize(float3(0.5f, 1.0f, 0.3f));
    float3 viewDir = normalize(cameraPosition - input.worldPos);
    
    // Diffuse lighting
    float diffuse = max(dot(normal, lightDir), 0.0f);
    
    // Specular (simplified Blinn-Phong, modified by roughness)
    float3 halfDir = normalize(lightDir + viewDir);
    float specPower = lerp(256.0f, 4.0f, roughness);  // Roughness controls spec power
    float specular = pow(max(dot(normal, halfDir), 0.0f), specPower) * (1.0f - roughness);
    
    // Metallic workflow: interpolate between dielectric and metallic
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo.rgb, metallic);
    float3 specularColor = lerp(float3(1.0f, 1.0f, 1.0f), albedo.rgb, metallic);
    
    // Final lighting
    float ambient = 0.4f;
    float3 lighting = ambient + diffuse * (1.0f - metallic * 0.5f);  // Metals have less diffuse
    float3 finalColor = albedo.rgb * lighting + specularColor * specular * 0.3f;
    
    return float4(finalColor, albedo.a);
}
