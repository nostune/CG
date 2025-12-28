// Procedural Starfield Skybox Shader
// 程序化星空天空盒着色器 - 带视差效果

cbuffer SkyboxBuffer : register(b0)
{
    matrix viewProjection;
    float3 cameraPosition;
    float time;
    float3 parallaxOffset;   // 相机移动产生的视差偏移 (cameraPos * parallaxFactor)
    float parallaxFactor;    // 视差系数 (0.0001 = 几乎不动, 模拟无限远)
};

struct VS_INPUT
{
    float3 position : POSITION;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD0;  // 方向向量用于采样星空
};

// ============================================
// Vertex Shader
// ============================================
PS_INPUT VSMain(VS_INPUT input)
{
    PS_INPUT output;
    
    // 天空盒跟随相机，但应用极小的视差偏移
    // parallaxOffset = cameraPosition * parallaxFactor
    // 这样当相机移动时，星空有非常轻微的移动，产生深度感
    float3 worldPos = input.position * 1000.0f + cameraPosition - parallaxOffset;
    
    output.position = mul(float4(worldPos, 1.0f), viewProjection);
    
    // 将深度设置为最远 (z = w，使得 z/w = 1.0)
    output.position.z = output.position.w;
    
    // 使用原始顶点位置作为方向向量（用于星空生成）
    output.texCoord = input.position;
    
    return output;
}

// ============================================
// 噪声和随机函数
// ============================================

// 伪随机函数
float hash(float3 p)
{
    p = frac(p * 0.3183099 + 0.1);
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

// 3D 噪声
float noise(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    
    return lerp(
        lerp(
            lerp(hash(i + float3(0, 0, 0)), hash(i + float3(1, 0, 0)), f.x),
            lerp(hash(i + float3(0, 1, 0)), hash(i + float3(1, 1, 0)), f.x),
            f.y
        ),
        lerp(
            lerp(hash(i + float3(0, 0, 1)), hash(i + float3(1, 0, 1)), f.x),
            lerp(hash(i + float3(0, 1, 1)), hash(i + float3(1, 1, 1)), f.x),
            f.y
        ),
        f.z
    );
}

// ============================================
// 星空生成
// ============================================

float stars(float3 dir, float density, float brightness)
{
    // 将方向归一化
    dir = normalize(dir);
    
    // 使用多层网格来生成不同大小的星星
    float stars = 0.0;
    
    // 层1：密集小星星
    float3 p1 = dir * 200.0;
    float n1 = hash(floor(p1));
    float d1 = length(frac(p1) - 0.5);
    if (n1 > (1.0 - density * 0.3) && d1 < 0.1)
    {
        stars += brightness * 0.5 * smoothstep(0.1, 0.0, d1);
    }
    
    // 层2：中等星星
    float3 p2 = dir * 100.0;
    float n2 = hash(floor(p2));
    float d2 = length(frac(p2) - 0.5);
    if (n2 > (1.0 - density * 0.15) && d2 < 0.15)
    {
        stars += brightness * 0.8 * smoothstep(0.15, 0.0, d2);
    }
    
    // 层3：大亮星星
    float3 p3 = dir * 50.0;
    float n3 = hash(floor(p3));
    float d3 = length(frac(p3) - 0.5);
    if (n3 > (1.0 - density * 0.05) && d3 < 0.2)
    {
        // 添加闪烁效果
        float twinkle = 0.7 + 0.3 * sin(time * 2.0 + n3 * 100.0);
        stars += brightness * twinkle * smoothstep(0.2, 0.0, d3);
    }
    
    return stars;
}

// 银河效果
float milkyWay(float3 dir)
{
    dir = normalize(dir);
    
    // 银河带 - 沿着某个平面分布
    float3 galaxyNormal = normalize(float3(0.2, 1.0, 0.1));
    float galaxyDist = abs(dot(dir, galaxyNormal));
    
    // 银河核心更亮
    float core = exp(-galaxyDist * 8.0);
    
    // 添加噪声纹理
    float n = noise(dir * 10.0) * 0.5 + noise(dir * 20.0) * 0.3 + noise(dir * 40.0) * 0.2;
    
    return core * n * 0.3;
}

// ============================================
// Pixel Shader
// ============================================
float4 PSMain(PS_INPUT input) : SV_TARGET
{
    float3 dir = normalize(input.texCoord);
    
    // 深空背景色（非常暗的蓝色）
    float3 spaceColor = float3(0.01, 0.01, 0.02);
    
    // 添加星星
    float starField = stars(dir, 0.8, 1.5);
    
    // 添加银河
    float milky = milkyWay(dir);
    float3 milkyColor = float3(0.6, 0.5, 0.8) * milky;
    
    // 最终颜色
    float3 finalColor = spaceColor + starField + milkyColor;
    
    // 轻微的大气散射效果（地平线附近略亮）
    float horizon = 1.0 - abs(dir.y);
    finalColor += float3(0.02, 0.01, 0.03) * horizon * horizon;
    
    return float4(finalColor, 1.0);
}
