// Basic shader for rendering simple geometry

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

struct VS_INPUT
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
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
    
    return output;
}

// Pixel Shader
float4 PSMain(PS_INPUT input) : SV_TARGET
{
    // Simple lighting
    float3 lightDir = normalize(float3(0.5f, -1.0f, 0.3f));
    float3 normal = normalize(input.normal);
    
    float diffuse = max(dot(normal, -lightDir), 0.0f);
    float ambient = 0.3f;
    
    float3 lighting = float3(ambient + diffuse, ambient + diffuse, ambient + diffuse);
    float3 finalColor = color.rgb * lighting;
    
    return float4(finalColor, color.a);
}
