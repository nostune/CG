# 多纹理模型加载总结

## 📦 系统架构

引擎已扩展为完整的PBR工作流，支持FBX/GLTF/OBJ等多格式模型，每个物体可绑定4张纹理（Albedo、Normal、Metallic、Roughness）到不同的shader槽位（t0-t3）。

## 🔧 核心实现

**1. AssimpLoader**：统一模型加载器，自动解析材质并提取所有PBR纹理路径，支持嵌入纹理检测。

**2. Material扩展**：新增`albedoTextureSRV`等4个GPU资源指针，替代单一`shaderProgram`字段，保持向后兼容。

**3. RenderQueue改造**：`RenderBatch`支持4纹理槽位，绘制时通过状态缓存机制仅在切换时绑定，避免冗余API调用。

**4. PBR Shader**：实现切线空间法线映射、金属度-粗糙度工作流、简化Blinn-Phong高光，支持纹理缺失时的优雅降级。

## 💡 使用方式

```cpp
// FBX多纹理加载
auto material = SceneAssetLoader::CreatePBRMaterial(
    device,
    "assets/models/spacecraft/texture_diffuse_00.png",
    "assets/models/spacecraft/texture_normal_00.png",
    "assets/models/spacecraft/texture_metallic_00.png",
    "assets/models/spacecraft/texture_roughness_00.png"
);

// 或使用AssimpLoader自动提取（推荐）
LoadedModel model;
AssimpLoader::LoadFromFile("assets/models/spacecraft/base.fbx", model);
model.mesh->CreateGPUBuffers(device);
```

RenderQueue自动处理多纹理绑定，每帧按shader-material-depth三级排序批次，最小化状态切换开销。
