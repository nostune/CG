// Textured shader for rendering models with diffuse texture

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

// Texture and sampler
Texture2D albedoTexture : register(t0);
SamplerState textureSampler : register(s0);

struct VS_INPUT
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;  // 顶点颜色（来自MTL的Kd）
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;  // 传递顶点颜色
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
    
    // Transform normal to world space
    output.normal = mul(input.normal, (float3x3)world);
    output.normal = normalize(output.normal);
    
    output.texcoord = input.texcoord;
    output.color = input.color;  // 传递顶点颜色
    
    return output;
}

// Pixel Shader
float4 PSMain(PS_INPUT input) : SV_TARGET
{
    // 尝试采样纹理
    float4 texColor = albedoTexture.Sample(textureSampler, input.texcoord);
    
    // 如果纹理采样失败或接近白色（默认值），使用顶点颜色
    // 判断标准：如果纹理是纯白色且顶点有颜色，则使用顶点颜色
    bool hasVertexColor = (input.color.r != 1.0f || input.color.g != 1.0f || input.color.b != 1.0f);
    if (hasVertexColor && texColor.r > 0.99f && texColor.g > 0.99f && texColor.b > 0.99f) {
        texColor = input.color;  // 使用顶点颜色
    }
    
    // Simple directional lighting
    float3 lightDir = normalize(float3(0.5f, -1.0f, 0.3f));
    float3 normal = normalize(input.normal);
    
    float diffuse = max(dot(normal, -lightDir), 0.2f);  // Min 0.2 ambient
    float ambient = 0.4f;
    
    float lighting = saturate(ambient + diffuse);
    
    // Combine texture with lighting
    float3 finalColor = texColor.rgb * lighting;
    
    return float4(finalColor, texColor.a);
}
