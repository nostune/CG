#pragma once

#include "../core/ECS.h"
#include "RenderBackend.h"
#include "LightweightRenderQueue.h"
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

class RenderSystem : public System {
public:
    RenderSystem() = default;
    ~RenderSystem() override = default;

    void Initialize(SceneManager* sceneManager);
    void Update(float deltaTime, entt::registry& registry) override;

    bool InitializeBackend(void* hwnd, int width, int height);
    RenderBackend* GetBackend() { return m_Backend.get(); }

private:
    void RenderScene(components::CameraComponent* camera, entt::registry& registry, bool shouldDebug);
    components::CameraComponent* FindActiveCamera(entt::registry& registry);
    
    // 更新所有实体的渲染优先级（距离、LOD等）
    void UpdateRenderPriorities(entt::registry& registry, const DirectX::XMFLOAT3& cameraPos);
    
    // 绘制队列中的实体
    void DrawQueue(const LightweightRenderQueue& queue, entt::registry& registry);

    std::unique_ptr<RenderBackend> m_Backend;
    SceneManager* m_SceneManager = nullptr;
    int m_UpdateCounter = 0;
    
    // 渲染队列（轻量级，只存Entity ID）
    LightweightRenderQueue m_OpaqueQueue;      // 不透明物体队列
    LightweightRenderQueue m_TransparentQueue; // 透明物体队列（未来用于水、大气等）
};

} // namespace outer_wilds
