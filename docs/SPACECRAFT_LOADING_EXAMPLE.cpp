// 多纹理FBX模型加载示例 - Spacecraft

#include "scene/SceneAssetLoader.h"
#include "graphics/resources/AssimpLoader.h"

// ============================================================================
// 方法1：使用AssimpLoader自动提取所有纹理（推荐）
// ============================================================================
void LoadSpacecraftWithAssimp(entt::registry& registry, ID3D11Device* device) {
    using namespace outer_wilds::resources;
    
    // 1. 加载FBX模型
    LoadedModel model;
    if (!AssimpLoader::LoadFromFile("assets/models/spacecraft/base_basic_pbr.fbx", model)) {
        // 加载失败处理
        return;
    }
    
    // 2. 创建GPU资源
    model.mesh->CreateGPUBuffers(device);
    
    // 3. 创建PBR材质（AssimpLoader已提取纹理路径）
    auto material = SceneAssetLoader::CreatePBRMaterial(
        device,
        model.texturePaths[LoadedModel::ALBEDO],    // texture_diffuse_00.png
        model.texturePaths[LoadedModel::NORMAL],    // texture_normal_00.png
        model.texturePaths[LoadedModel::METALLIC],  // texture_metallic_00.png
        model.texturePaths[LoadedModel::ROUGHNESS]  // texture_roughness_00.png
    );
    
    // 4. 创建Entity并添加组件
    entt::entity entity = registry.create();
    registry.emplace<TransformComponent>(entity, 
        DirectX::XMFLOAT3(0, 5, 0),  // position
        DirectX::XMFLOAT3(1, 1, 1),  // scale
        DirectX::XMFLOAT4(0, 0, 0, 1) // rotation
    );
    registry.emplace<MeshComponent>(entity, model.mesh, material);
    registry.emplace<RenderPriorityComponent>(entity);
}

// ============================================================================
// 方法2：手动指定每个纹理路径
// ============================================================================
void LoadSpacecraftManual(entt::registry& registry, ID3D11Device* device) {
    using namespace outer_wilds;
    
    // 1. 加载mesh（使用OBJ或手动加载FBX）
    auto mesh = SceneAssetLoader::LoadMeshResource("assets/models/spacecraft/base.fbx");
    mesh->CreateGPUBuffers(device);
    
    // 2. 创建PBR材质（显式指定所有纹理）
    auto material = SceneAssetLoader::CreatePBRMaterial(
        device,
        "assets/models/spacecraft/texture_diffuse_00.png",
        "assets/models/spacecraft/texture_normal_00.png",
        "assets/models/spacecraft/texture_metallic_00.png",
        "assets/models/spacecraft/texture_roughness_00.png"
    );
    
    // 3. 创建Entity
    entt::entity entity = registry.create();
    registry.emplace<TransformComponent>(entity, 
        DirectX::XMFLOAT3(0, 5, 0), 
        DirectX::XMFLOAT3(1, 1, 1), 
        DirectX::XMFLOAT4(0, 0, 0, 1)
    );
    registry.emplace<MeshComponent>(entity, mesh, material);
}

// ============================================================================
// 方法3：只使用部分纹理（其他纹理留空）
// ============================================================================
void LoadSpacecraftPartialTextures(entt::registry& registry, ID3D11Device* device) {
    // 只加载diffuse和normal贴图，metallic/roughness留空
    auto material = SceneAssetLoader::CreatePBRMaterial(
        device,
        "assets/models/spacecraft/texture_diffuse_00.png",
        "assets/models/spacecraft/texture_normal_00.png",
        "",  // no metallic map
        ""   // no roughness map
    );
    
    // Shader会自动检测并使用默认值：metallic=0.0, roughness=0.5
}

// ============================================================================
// 渲染说明
// ============================================================================
/*
 * RenderQueue会自动处理多纹理绑定：
 * 
 * 1. CollectFromECS阶段：
 *    - 检测material中的4个纹理SRV
 *    - 自动选择textured.vs/ps shader
 *    - 构建RenderBatch并填充4个纹理槽位
 * 
 * 2. Execute阶段：
 *    - 绑定t0=albedo, t1=normal, t2=metallic, t3=roughness
 *    - 使用状态缓存，仅在切换时调用PSSetShaderResources
 *    - 按shader-material-depth排序，最小化draw call
 * 
 * 3. Shader计算：
 *    - 采样4张贴图
 *    - 法线映射（切线空间 -> 世界空间）
 *    - PBR光照（金属度-粗糙度工作流）
 *    - 纹理缺失时优雅降级
 */
