#pragma once
#include "../core/ECS.h"
#include <d3d11.h>
#include <string>
#include <memory>

namespace outer_wilds {

// UI组件 - 标记实体有UI元素
struct UIComponent {
    bool visible = true;
};

// 欢迎界面状态
enum class WelcomeScreenState {
    FadeIn,      // 淡入
    Display,     // 显示
    FadeOut,     // 淡出
    Hidden       // 隐藏（不再显示）
};

class UISystem : public System {
public:
    UISystem();
    ~UISystem();

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, HWND hwnd);
    void Update(float deltaTime, entt::registry& registry) override;
    void Render();
    void Shutdown();

    // 欢迎界面控制
    void ShowWelcomeScreen(const std::string& imagePath, float displayDuration = 3.0f);
    void ShowWelcomeScreenWithKeyWait(const std::string& imagePath);
    void HideWelcomeScreen();
    bool IsWelcomeScreenVisible() const { return m_WelcomeScreenState != WelcomeScreenState::Hidden; }
    bool IsWaitingForKeyPress() const { return m_WaitingForKeyPress; }
    bool WasKeyPressed() const { return m_KeyPressed; }

private:
    void RenderWelcomeScreen();
    bool LoadTextureFromFile(const std::string& filename, ID3D11ShaderResourceView** outSRV, int* outWidth, int* outHeight);

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;
    HWND m_Hwnd = nullptr;

    // 欢迎界面
    WelcomeScreenState m_WelcomeScreenState = WelcomeScreenState::Hidden;
    ID3D11ShaderResourceView* m_WelcomeTexture = nullptr;
    int m_WelcomeImageWidth = 0;
    int m_WelcomeImageHeight = 0;
    float m_WelcomeAlpha = 0.0f;
    float m_WelcomeTimer = 0.0f;
    float m_WelcomeDisplayDuration = 3.0f;
    float m_FadeDuration = 1.0f; // 淡入淡出持续时间
    bool m_WaitingForKeyPress = false;
    bool m_KeyPressed = false;

    bool m_ImGuiInitialized = false;
};

} // namespace outer_wilds
