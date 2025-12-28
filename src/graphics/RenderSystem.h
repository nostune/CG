#pragma once

#include "../core/ECS.h"
#include "RenderBackend.h"
#include "RenderQueue.h"
#include "SkyboxRenderer.h"
#include <memory>
#include <DirectXMath.h>

// Forward declarations
namespace outer_wilds {
    class Scene;
    class SceneManager;
    namespace components {
        class CameraComponent;
    }
}

namespace outer_wilds {

/**
 * @brief 渲染系统 v0.3.0
 * 
 * 更新说明：
 * - 使用新RenderQueue替代LightweightRenderQueue
 * - 支持状态缓存优化
 * - 提供手动添加接口（用于调试几何体）
 */
class RenderSystem : public System {
public:
    RenderSystem() = default;
    ~RenderSystem() override = default;

    void Initialize(SceneManager* sceneManager);
    void Update(float deltaTime, entt::registry& registry) override;

    bool InitializeBackend(void* hwnd, int width, int height);
    RenderBackend* GetBackend() { return m_Backend.get(); }
    
    /**
     * @brief 获取渲染队列（用于手动添加）
     * @return 不透明物体队列引用
     * 
     * 用例：在main.cpp中手动添加调试球体
     * @code
     * RenderBatch debugSphere;
     * debugSphere.vertexBuffer = sphereVB;
     * renderSystem->GetRenderQueue().AddBatch(debugSphere);
     * @endcode
     */
    RenderQueue& GetRenderQueue() { return m_RenderQueue; }
    
    /**
     * @brief 设置太阳实体（用于动态光照）
     * @param sunEntity 太阳实体ID
     */
    void SetSunEntity(entt::entity sunEntity) { m_SunEntity = sunEntity; }
    
    /**
     * @brief 获取天空盒渲染器
     */
    SkyboxRenderer* GetSkyboxRenderer() { return m_SkyboxRenderer.get(); }

private:
    void RenderScene(components::CameraComponent* camera, entt::registry& registry, bool shouldDebug);
    components::CameraComponent* FindActiveCamera(entt::registry& registry);
    DirectX::XMFLOAT3 GetSunPosition(entt::registry& registry);

    std::unique_ptr<RenderBackend> m_Backend;
    SceneManager* m_SceneManager = nullptr;
    int m_UpdateCounter = 0;
    float m_Time = 0.0f;  // 累积时间（用于shader动画）
    
    // v0.3.0: 统一渲染队列（支持状态缓存）
    RenderQueue m_RenderQueue;
    ID3D11Buffer* m_PerObjectCB = nullptr;  // PerObject常量缓冲区
    
    // 天空盒渲染器
    std::unique_ptr<SkyboxRenderer> m_SkyboxRenderer;
    
    // 太阳实体（用于动态光照）
    entt::entity m_SunEntity = entt::null;
};

} // namespace outer_wilds
