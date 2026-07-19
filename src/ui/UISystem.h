#pragma once
#include "../core/ECS.h"
#include <d3d11.h>
#include <string>
#include <memory>
#include <vector>
#include <DirectXMath.h>

namespace outer_wilds {

class PhysXDebugRenderer;

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

    void SetDiagnosticsVisible(bool visible) { m_ShowDiagnostics = visible; }
    bool IsDiagnosticsVisible() const { return m_ShowDiagnostics; }
    void SetNavigationMapVisible(bool visible) { m_ShowNavigationMap = visible; }
    bool IsNavigationMapVisible() const { return m_ShowNavigationMap; }

private:
    struct ObjectiveHudSnapshot {
        bool visible = false;
        std::string title;
        std::string description;
        float progress = 0.0f;
        float requiredProgress = 1.0f;
    };

    struct NavigationSnapshot {
        struct Body {
            entt::entity entity = entt::null;
            std::string name;
            DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
            float radius = 1.0f;
            bool currentSector = false;
        };

        struct Path {
            std::vector<DirectX::XMFLOAT3> points;
            bool dashed = false;
        };

        bool available = false;
        std::string bodyName;
        std::string trajectoryStatus;
        float bodyRadius = 0.0f;
        float altitude = 0.0f;
        float speed = 0.0f;
        float radialSpeed = 0.0f;
        float tangentialSpeed = 0.0f;
        float circularOrbitSpeed = 0.0f;
        bool circularizeActive = false;
        float periapsisAltitude = 0.0f;
        float apoapsisAltitude = 0.0f;
        std::vector<Body> bodies;
        std::vector<Path> bodyOrbits;
        Path predictedWorldTrajectory;
        DirectX::XMFLOAT3 spacecraftWorldPosition = {0.0f, 0.0f, 0.0f};
        bool hasSpacecraftWorldPosition = false;
        DirectX::XMFLOAT4X4 viewProjection = {};
        bool hasMapCamera = false;
        float cameraTransition = 0.0f;
        entt::entity lockedTarget = entt::null;
    };

    struct NavigationTargetSnapshot {
        bool hasCandidate = false;
        bool hasLockedTarget = false;
        bool matchingVelocity = false;
        DirectX::XMFLOAT3 candidateWorldPosition = {0.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT3 targetWorldPosition = {0.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT4X4 viewProjection = {};
        std::string targetName;
        float distance = 0.0f;
        float relativeSpeed = 0.0f;
    };

    void UpdateGameplaySnapshot(entt::registry& registry);
    void RenderObjectiveHud();
    void RenderNavigationMap();
    void RenderNavigationTargetHud();
    void RenderWelcomeScreen();
    void RenderDiagnosticsPanel();
    void RenderDiagnosticsOverview();
    void RenderDiagnosticsLogs();
    void RenderDiagnosticsPhysics();
    void RenderPhysXSpaceWindow();
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
    bool m_ShowDiagnostics = false;
    bool m_LogAutoScroll = true;
    int m_LogLevelFilter = 0;
    char m_LogTextFilter[128] = {};
    std::string m_ImGuiIniPath;
    std::unique_ptr<PhysXDebugRenderer> m_PhysXDebugRenderer;
    bool m_ShowPhysXSpace = false;
    ObjectiveHudSnapshot m_ObjectiveHud;
    NavigationSnapshot m_Navigation;
    NavigationTargetSnapshot m_NavigationTarget;
    bool m_ShowNavigationMap = false;
    bool m_MouseLookBeforeMap = true;
    float m_MapYaw = -0.55f;
    float m_MapPitch = 0.65f;
    float m_MapZoom = 1.0f;
    DirectX::XMFLOAT2 m_MapPan = {0.0f, 0.0f};
    bool m_MapCenteredOnSpacecraft = false;
    entt::entity m_SelectedMapBody = entt::null;
    entt::entity m_FocusedMapBody = entt::null;
    entt::entity m_PendingMapLockTarget = entt::null;
};

} // namespace outer_wilds
