#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <memory>

namespace outer_wilds {
namespace resources {
    class Shader;
}

/**
 * @brief 程序化星空天空盒渲染器
 * 
 * 特性：
 * - 程序化生成星空（无需纹理）
 * - 视差效果：相机移动时星空有极轻微的位移
 * - 银河和星星闪烁效果
 */
class SkyboxRenderer {
public:
    SkyboxRenderer() = default;
    ~SkyboxRenderer();

    /**
     * @brief 初始化天空盒资源
     * @param device D3D11设备
     * @return 是否成功
     */
    bool Initialize(ID3D11Device* device);

    /**
     * @brief 渲染天空盒
     * @param context D3D11上下文
     * @param viewProjection 视图投影矩阵
     * @param cameraPosition 相机位置
     * @param time 时间（用于动画）
     */
    void Render(
        ID3D11DeviceContext* context,
        const DirectX::XMMATRIX& viewProjection,
        const DirectX::XMFLOAT3& cameraPosition,
        float time
    );

    /**
     * @brief 设置视差系数
     * @param factor 视差系数 (默认0.0001, 越小星空看起来越远)
     */
    void SetParallaxFactor(float factor) { m_ParallaxFactor = factor; }

private:
    // GPU资源
    ID3D11Buffer* m_VertexBuffer = nullptr;
    ID3D11Buffer* m_IndexBuffer = nullptr;
    ID3D11Buffer* m_ConstantBuffer = nullptr;
    UINT m_IndexCount = 0;
    
    // 渲染状态
    ID3D11DepthStencilState* m_DepthState = nullptr;
    ID3D11RasterizerState* m_RasterizerState = nullptr;
    
    // Shader
    std::unique_ptr<resources::Shader> m_Shader;
    
    // 参数
    float m_ParallaxFactor = 0.0001f;  // 视差系数
    
    // 常量缓冲区结构
    struct SkyboxCB {
        DirectX::XMMATRIX viewProjection;
        DirectX::XMFLOAT3 cameraPosition;
        float time;
        DirectX::XMFLOAT3 parallaxOffset;
        float parallaxFactor;
    };
    
    bool CreateSphere(ID3D11Device* device);
    bool CreateShader(ID3D11Device* device);
    bool CreateRenderStates(ID3D11Device* device);
};

} // namespace outer_wilds
