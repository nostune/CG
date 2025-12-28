# GLB/GLTF 模型加载指南

## 概述

引擎现已支持 GLB/GLTF 格式的完整加载，包括：
- **嵌入式纹理** - GLB 文件内的纹理自动提取和加载
- **PBR 材质** - 自动提取 Albedo、Normal、Metallic、Roughness、AO、Emissive 纹理
- **切线空间** - 自动计算切线用于法线贴图

## 支持的格式

| 格式 | 扩展名 | 嵌入式纹理 | 备注 |
|------|--------|-----------|------|
| GLTF Binary | `.glb` | ✅ 完全支持 | 推荐格式，所有资源打包在一个文件 |
| GLTF | `.gltf` | ⚠️ 外部引用 | 需要外部纹理文件 |
| FBX | `.fbx` | ✅ 支持 | 二进制格式 |
| OBJ | `.obj` | ❌ | 通过 MTL 文件引用纹理 |
| DAE | `.dae` | ⚠️ | Collada 格式 |

## 基本使用

### 方式1：SceneAssetLoader（推荐）

```cpp
#include "scene/SceneAssetLoader.h"

// 加载 GLB 模型作为实体
auto entity = SceneAssetLoader::LoadModelAsEntity(
    registry,                  // ECS registry
    scene,                     // 当前场景
    device,                    // D3D11 设备
    "assets/models/moon.glb",  // GLB 文件路径
    "",                        // 纹理路径（留空，使用嵌入式纹理）
    {0.0f, 100.0f, 0.0f},     // 位置
    {1.0f, 1.0f, 1.0f}        // 缩放
);
```

### 方式2：AssimpLoader 直接使用

```cpp
#include "graphics/resources/AssimpLoader.h"

using namespace outer_wilds::resources;

LoadedModel model;
if (AssimpLoader::LoadFromFile("assets/models/moon.glb", model)) {
    // 获取网格
    auto mesh = model.mesh;
    mesh->CreateGPUBuffers(device);
    
    // 检查嵌入式纹理
    if (model.HasEmbeddedTexture(LoadedModel::ALBEDO)) {
        ID3D11ShaderResourceView* albedoSRV = nullptr;
        AssimpLoader::CreateTextureFromEmbedded(
            device, 
            model.embeddedTextures[LoadedModel::ALBEDO],
            &albedoSRV
        );
    }
    
    // 打印模型信息
    std::cout << "Vertices: " << mesh->GetVertices().size() << std::endl;
    std::cout << "Triangles: " << mesh->GetIndices().size() / 3 << std::endl;
}
```

## 获取模型尺寸（Bounding Box）

```cpp
// 计算模型边界盒
DirectX::XMFLOAT3 minBounds = {FLT_MAX, FLT_MAX, FLT_MAX};
DirectX::XMFLOAT3 maxBounds = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

for (const auto& vertex : model.mesh->GetVertices()) {
    minBounds.x = std::min(minBounds.x, vertex.position.x);
    minBounds.y = std::min(minBounds.y, vertex.position.y);
    minBounds.z = std::min(minBounds.z, vertex.position.z);
    maxBounds.x = std::max(maxBounds.x, vertex.position.x);
    maxBounds.y = std::max(maxBounds.y, vertex.position.y);
    maxBounds.z = std::max(maxBounds.z, vertex.position.z);
}

float sizeX = maxBounds.x - minBounds.x;
float sizeY = maxBounds.y - minBounds.y;
float sizeZ = maxBounds.z - minBounds.z;

std::cout << "Model size: " << sizeX << " x " << sizeY << " x " << sizeZ << std::endl;
```

## 纹理类型索引

```cpp
enum TextureType { 
    ALBEDO = 0,     // 基础颜色/漫反射
    NORMAL = 1,     // 法线贴图
    METALLIC = 2,   // 金属度
    ROUGHNESS = 3,  // 粗糙度
    AO = 4,         // 环境光遮蔽
    EMISSIVE = 5    // 自发光
};
```

## 注意事项

1. **GLB 优先使用嵌入式纹理** - 如果 GLB 包含嵌入式纹理，会优先使用
2. **自动格式检测** - 根据文件扩展名自动选择加载器
3. **坐标系转换** - Assimp 会自动翻转 UV 以适配 DirectX
4. **切线计算** - 自动计算切线空间用于法线贴图

## 故障排除

### 纹理不显示
- 检查 GLB 文件是否包含嵌入式纹理
- 查看控制台日志中的纹理加载信息

### 模型太大或太小
- 使用边界盒计算确定原始尺寸
- 调整 scale 参数

### 法线贴图效果不对
- 确保模型有 UV 坐标
- 检查法线贴图格式（应为切线空间法线）
