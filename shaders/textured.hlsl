// PBR Textured shader with multi-texture support and dynamic sun lighting

cbuffer PerFrameBuffer : register(b0)
{
    matrix viewProjection;
    float3 cameraPosition;
    float time;
    float3 sunPosition;      // 太阳世界坐标
    float sunIntensity;      // 太阳光强度
    float3 sunColor;         // 太阳光颜色
    float ambientStrength;   // 环境光强度
};

cbuffer PerObjectBuffer : register(b1)
{
    matrix world;
    float4 color;
    float3 lightDir;      // CPU预计算的光照方向（从物体指向太阳）
    float isSphere;       // 是否是球体
};

// Multi-texture PBR maps
Texture2D albedoTexture : register(t0);    // Diffuse/Albedo map
Texture2D normalTexture : register(t1);    // Normal map (tangent space)
Texture2D metallicTexture : register(t2);  // Metallic map
Texture2D roughnessTexture : register(t3); // Roughness map
Texture2D emissiveTexture : register(t4);  // Emissive/自发光 map
SamplerState textureSampler : register(s0);

// Material properties
cbuffer MaterialBuffer : register(b2)
{
    float3 emissiveColor;    // 自发光颜色
    float emissiveStrength;  // 发光强度 (0 = 无发光)
    float hasEmissiveTexture; // 是否有emissive纹理 (1.0 = 是, 0.0 = 否)
    float3 padding2;         // Padding to 32 bytes
};

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
    float3 worldPos : TEXCOORD1;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float3 tangent : TANGENT;
    float4 color : COLOR0;
    float3 modelPos : TEXCOORD2;  // 模型空间位置（用于计算球面法线）
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
    
    // 传递模型空间位置（用于计算球面法线）
    output.modelPos = input.position;
    
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
    
    // 使用模型法线（新模型有正确的平滑法线）
    float3 normal = normalize(input.normal);
    
    float3 viewDir = normalize(cameraPosition - input.worldPos);
    
    // Sample normal map (if available) and apply tangent-space normal mapping
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
    
    // ============================================
    // 基于太阳位置的动态光照（点光源模式）
    // ============================================
    
    // ====== 数值调试模式 ======
    // 0=正常渲染, 1=sunPosition, 2=worldPos, 3=normal, 4=NdotL, 5=cpuLightDir, 6=gpuLightDir
    // 10=半球光照测试
    #define DEBUG_MODE 0
    
    // ====== 光照模式 ======
    // 0=标准NdotL, 1=半球光照（基于几何位置）, 2=wrap光照
    #define LIGHTING_MODE 1
    
    // === 计算光照方向 ===
    float3 toSun = input.worldPos - sunPosition;  // 从太阳指向像素（取反后的）
    float distToSun = length(toSun);
    
    float3 gpuLightDir;
    if (distToSun < 1.0f) {
        gpuLightDir = float3(0.0f, 1.0f, 0.0f);
    } else {
        gpuLightDir = toSun / distToSun;
    }
    
    float3 pixelLightDir = gpuLightDir;
    float NdotL = dot(normal, pixelLightDir);
    
    // === 半球光照：基于世界位置判断是否面向太阳 ===
    // 计算像素到物体中心的方向（用世界矩阵的平移部分作为物体中心）
    float3 objectCenter = float3(world._41, world._42, world._43);
    float3 sunToCenter = objectCenter - sunPosition;  // 从太阳到物体中心
    float3 centerToPixel = input.worldPos - objectCenter;  // 从物体中心到像素
    
    // 如果centerToPixel和sunToCenter同向，说明像素在背向太阳的半球
    // 如果centerToPixel和sunToCenter反向，说明像素在面向太阳的半球
    float hemisphereTest = dot(normalize(centerToPixel), normalize(sunToCenter));
    
    // hemisphereTest > 0 = 背光面, < 0 = 面光面
    float hemisphereLighting = saturate(-hemisphereTest);  // 面光面=1, 背光面=0
    
    // 软化边缘过渡
    float softHemisphere = smoothstep(-0.1f, 0.3f, -hemisphereTest);
    
    #if DEBUG_MODE == 1
        return float4(sunPosition / 200.0f + 0.5f, 1.0f);
    #elif DEBUG_MODE == 2
        return float4(input.worldPos / 400.0f + 0.5f, 1.0f);
    #elif DEBUG_MODE == 3
        return float4(normal * 0.5f + 0.5f, 1.0f);
    #elif DEBUG_MODE == 4
        // NdotL 可视化
        float vis = NdotL * 0.5f + 0.5f;
        return float4(1.0f - vis, vis, 0.0f, 1.0f);
    #elif DEBUG_MODE == 5
        return float4(normalize(lightDir) * 0.5f + 0.5f, 1.0f);
    #elif DEBUG_MODE == 6
        return float4(gpuLightDir * 0.5f + 0.5f, 1.0f);
    #elif DEBUG_MODE == 10
        // 半球光照可视化：绿=面光，红=背光
        return float4(1.0f - softHemisphere, softHemisphere, 0.0f, 1.0f);
    #endif
    
    // === 根据光照模式计算漫反射 ===
    float diffuse;
    
    #if LIGHTING_MODE == 0
        // 标准 NdotL 光照
        float wrap = 0.3f;
        diffuse = max((NdotL + wrap) / (1.0f + wrap), 0.0f);
    #elif LIGHTING_MODE == 1
        // 半球光照：基于几何位置，不依赖法线
        // 面光半球 = 1.0，背光半球 = 环境光
        diffuse = softHemisphere * 0.8f + 0.2f;  // 面光=1.0, 背光=0.2
    #elif LIGHTING_MODE == 2
        // Wrap 光照 + 半球混合
        float wrap = 0.5f;
        float wrapDiffuse = max((NdotL + wrap) / (1.0f + wrap), 0.0f);
        diffuse = lerp(wrapDiffuse, softHemisphere, 0.5f);  // 50%混合
    #endif
    
    // Specular (simplified Blinn-Phong, modified by roughness)
    float3 halfDir = normalize(pixelLightDir + viewDir);
    float specPower = lerp(256.0f, 4.0f, roughness);  // Roughness controls spec power
    float specular = pow(max(dot(normal, halfDir), 0.0f), specPower) * (1.0f - roughness);
    
    // 半球模式下减弱高光（因为不依赖法线）
    #if LIGHTING_MODE == 1
        specular *= softHemisphere * 0.5f;
    #endif
    
    // Metallic workflow: interpolate between dielectric and metallic
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo.rgb, metallic);
    float3 specularColor = lerp(float3(1.0f, 1.0f, 1.0f), albedo.rgb, metallic);
    
    // Final lighting with sun color
    // 环境光：模拟来自星空和其他光源的间接光照
    float3 ambientColor = float3(0.2f, 0.2f, 0.25f);  // 偏蓝的深空环境光
    float3 ambient = ambientStrength * ambientColor;
    
    float3 diffuseLight = diffuse * sunColor * sunIntensity;
    float3 specularLight = specularColor * specular * sunColor * sunIntensity * 0.3f;
    
    // 最终光照 = 环境光 + 漫反射 + 高光
    float3 lighting = ambient + diffuseLight * (1.0f - metallic * 0.5f);
    float3 finalColor = albedo.rgb * lighting + specularLight;
    
    // Emissive/自发光 - 采样emissive纹理或使用emissive颜色
    float3 emissive = float3(0.0f, 0.0f, 0.0f);
    if (hasEmissiveTexture > 0.5f) {
        // 有emissive纹理时，采样纹理并乘以emissive颜色和强度
        emissive = emissiveTexture.Sample(textureSampler, input.texcoord).rgb;
        emissive *= emissiveColor;
        // 自发光物体（如太阳）：直接使用emissive颜色，忽略光照计算
        finalColor = emissive * emissiveStrength;
    } else {
        // 没有纹理时，检查是否有emissive颜色
        float emissiveTotal = emissiveColor.r + emissiveColor.g + emissiveColor.b;
        if (emissiveTotal > 0.1f && emissiveStrength > 0.1f) {
            // 有emissive颜色，直接使用
            finalColor = emissiveColor * emissiveStrength;
        } else {
            // 普通物体，使用计算的光照
            finalColor += emissive * emissiveStrength;
        }
    }
    
    return float4(finalColor, albedo.a);
}
