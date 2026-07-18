#include "UISystem.h"
#include "../core/DebugManager.h"
#include "../core/ProjectPaths.h"
#include "../core/TimeManager.h"
#include "../physics/PhysXManager.h"
#include "../graphics/debug/PhysXDebugRenderer.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <Windows.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace outer_wilds {

namespace {

bool EnvironmentFlagEnabled(const char* name) {
    char value[16] = {};
    const DWORD length = GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value)));
    return length > 0 && length < sizeof(value) && std::string(value) != "0";
}

ImVec4 LogLevelColor(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return ImVec4(0.55f, 0.58f, 0.62f, 1.0f);
        case LogLevel::Debug: return ImVec4(0.45f, 0.72f, 0.95f, 1.0f);
        case LogLevel::Info: return ImVec4(0.82f, 0.84f, 0.88f, 1.0f);
        case LogLevel::Warning: return ImVec4(0.96f, 0.72f, 0.25f, 1.0f);
        case LogLevel::Error: return ImVec4(0.96f, 0.35f, 0.30f, 1.0f);
        case LogLevel::Critical: return ImVec4(1.0f, 0.18f, 0.45f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

bool ContainsCaseInsensitive(const std::string& text, const char* filter) {
    if (filter == nullptr || filter[0] == '\0') {
        return true;
    }
    std::string haystack = text;
    std::string needle = filter;
    std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return haystack.find(needle) != std::string::npos;
}

} // namespace

UISystem::UISystem() {
}

UISystem::~UISystem() {
    Shutdown();
}

bool UISystem::Initialize(ID3D11Device* device, ID3D11DeviceContext* context, HWND hwnd) {
    m_Device = device;
    m_Context = context;
    m_Hwnd = hwnd;
    const bool startPhysXDebugView = EnvironmentFlagEnabled("OUTERWILDS_PHYSX_DEBUG_VIEW");
    m_ShowDiagnostics = EnvironmentFlagEnabled("OUTERWILDS_DIAGNOSTICS") || startPhysXDebugView;
    m_ShowPhysXSpace = startPhysXDebugView;
    if (startPhysXDebugView) {
        PhysXManager::GetInstance().SetDebugVisualizationEnabled(true);
    }

    // 初始化ImGui上下文
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    const auto settingsDirectory = ProjectPaths::Root() / "saved";
    std::error_code settingsError;
    std::filesystem::create_directories(settingsDirectory, settingsError);
    m_ImGuiIniPath = (settingsDirectory / "imgui.ini").string();
    io.IniFilename = m_ImGuiIniPath.c_str();

    // 设置ImGui样式
    ImGui::StyleColorsDark();

    // 初始化平台/渲染器绑定
    if (!ImGui_ImplWin32_Init(hwnd)) {
        std::cerr << "Failed to initialize ImGui Win32 backend" << std::endl;
        return false;
    }

    if (!ImGui_ImplDX11_Init(device, context)) {
        std::cerr << "Failed to initialize ImGui DX11 backend" << std::endl;
        ImGui_ImplWin32_Shutdown();
        return false;
    }

    m_ImGuiInitialized = true;
    m_PhysXDebugRenderer = std::make_unique<PhysXDebugRenderer>();
    if (!m_PhysXDebugRenderer->Initialize(device)) {
        DebugManager::GetInstance().Warning("PhysXDebugView", "PhysX Space renderer initialization failed");
        m_PhysXDebugRenderer.reset();
    }
    std::cout << "UISystem initialized successfully" << std::endl;

    return true;
}

void UISystem::Update(float deltaTime, entt::registry& registry) {
    if (!m_ImGuiInitialized) return;

    if (GetAsyncKeyState(VK_F1) & 1) {
        m_ShowDiagnostics = !m_ShowDiagnostics;
        DebugManager::GetInstance().Info(
            "Diagnostics", m_ShowDiagnostics ? "Diagnostics panel opened" : "Diagnostics panel closed");
    }

    // 更新欢迎界面状态
    if (m_WelcomeScreenState != WelcomeScreenState::Hidden) {
        m_WelcomeTimer += deltaTime;

        switch (m_WelcomeScreenState) {
            case WelcomeScreenState::FadeIn:
                m_WelcomeAlpha = m_WelcomeTimer / m_FadeDuration;
                if (m_WelcomeAlpha >= 1.0f) {
                    m_WelcomeAlpha = 1.0f;
                    m_WelcomeTimer = 0.0f;
                    m_WelcomeScreenState = WelcomeScreenState::Display;
                }
                break;

            case WelcomeScreenState::Display:
                // 如果等待按键，检查是否有按键按下
                if (m_WaitingForKeyPress) {
                    bool anyKeyPressed = false;
                    
                    // 使用Windows原生API检测按键 - 更可靠
                    // 检查常用按键
                    if (GetAsyncKeyState(VK_SPACE) & 0x8000 ||
                        GetAsyncKeyState(VK_RETURN) & 0x8000 ||
                        GetAsyncKeyState(VK_ESCAPE) & 0x8000 ||
                        GetAsyncKeyState(VK_LBUTTON) & 0x8000 ||
                        GetAsyncKeyState(VK_RBUTTON) & 0x8000) {
                        anyKeyPressed = true;
                    }
                    
                    // 检查字母键
                    for (int key = 'A'; key <= 'Z'; key++) {
                        if (GetAsyncKeyState(key) & 0x8000) {
                            anyKeyPressed = true;
                            break;
                        }
                    }
                    
                    // 检查数字键
                    for (int key = '0'; key <= '9'; key++) {
                        if (GetAsyncKeyState(key) & 0x8000) {
                            anyKeyPressed = true;
                            break;
                        }
                    }
                    
                    if (anyKeyPressed) {
                        m_KeyPressed = true;
                        m_WelcomeTimer = 0.0f;
                        m_WelcomeScreenState = WelcomeScreenState::FadeOut;
                        std::cout << "Key detected! Transitioning to game..." << std::endl;
                    }
                } else if (m_WelcomeTimer >= m_WelcomeDisplayDuration) {
                    m_WelcomeTimer = 0.0f;
                    m_WelcomeScreenState = WelcomeScreenState::FadeOut;
                }
                break;

            case WelcomeScreenState::FadeOut:
                m_WelcomeAlpha = 1.0f - (m_WelcomeTimer / m_FadeDuration);
                if (m_WelcomeAlpha <= 0.0f) {
                    m_WelcomeAlpha = 0.0f;
                    m_WelcomeScreenState = WelcomeScreenState::Hidden;
                    
                    // 清理纹理
                    if (m_WelcomeTexture) {
                        m_WelcomeTexture->Release();
                        m_WelcomeTexture = nullptr;
                    }
                }
                break;

            default:
                break;
        }
    }
}

void UISystem::Render() {
    if (!m_ImGuiInitialized) return;

    // 开始新的ImGui帧
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // 渲染欢迎界面
    if (m_WelcomeScreenState != WelcomeScreenState::Hidden && m_WelcomeTexture) {
        RenderWelcomeScreen();
    }

    if (m_ShowDiagnostics) {
        RenderDiagnosticsPanel();
    }
    if (m_ShowPhysXSpace) {
        RenderPhysXSpaceWindow();
    }

    // 结束ImGui帧并渲染
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void UISystem::RenderDiagnosticsPanel() {
    ImGui::SetNextWindowSize(ImVec2(760.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Diagnostics", &m_ShowDiagnostics, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginMenuBar()) {
        ImGui::TextUnformatted("F1 toggles this panel");
        ImGui::EndMenuBar();
    }

    if (ImGui::BeginTabBar("DiagnosticsTabs")) {
        if (ImGui::BeginTabItem("Overview")) {
            RenderDiagnosticsOverview();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Logs")) {
            RenderDiagnosticsLogs();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Physics")) {
            RenderDiagnosticsPhysics();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void UISystem::RenderDiagnosticsOverview() {
    const auto& time = TimeManager::GetInstance();
    const float fps = time.GetFPS();
    const float frameMs = time.GetDeltaTime() * 1000.0f;

    ImGui::Text("FPS: %.1f", fps);
    ImGui::SameLine(180.0f);
    ImGui::Text("Frame: %.2f ms", frameMs);
    ImGui::Separator();

    auto& diagnostics = DebugManager::GetInstance();
    ImGui::Text("Buffered logs: %zu", diagnostics.GetEntryCount());
    ImGui::Text("Dropped logs: %llu", static_cast<unsigned long long>(diagnostics.GetDroppedEntryCount()));
    ImGui::TextWrapped("Session log: %s", diagnostics.GetSessionLogPath().string().c_str());

    ImGui::SeparatorText("Metrics");
    for (const auto& metric : diagnostics.GetMetrics()) {
        ImGui::Text("%-26s %10.2f %s", metric.name.c_str(), metric.value, metric.unit.c_str());
    }
}

void UISystem::RenderDiagnosticsLogs() {
    static const char* levelNames[] = {"Trace+", "Debug+", "Info+", "Warning+", "Error+", "Critical"};
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("Level", &m_LogLevelFilter, levelNames, IM_ARRAYSIZE(levelNames));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("##LogFilter", "category or message", m_LogTextFilter, IM_ARRAYSIZE(m_LogTextFilter));
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_LogAutoScroll);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        DebugManager::GetInstance().ClearEntries();
    }

    bool consoleEnabled = DebugManager::GetInstance().IsConsoleEnabled();
    if (ImGui::Checkbox("Mirror framework logs to console", &consoleEnabled)) {
        DebugManager::GetInstance().SetConsoleEnabled(consoleEnabled);
    }

    const auto entries = DebugManager::GetInstance().GetEntries();
    ImGui::BeginChild("LogEntries", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;
    for (const auto& entry : entries) {
        if (static_cast<int>(entry.level) < m_LogLevelFilter) {
            continue;
        }
        const std::string searchable = entry.category + " " + entry.message;
        if (!ContainsCaseInsensitive(searchable, m_LogTextFilter)) {
            continue;
        }

        char prefix[128] = {};
        std::snprintf(
            prefix, sizeof(prefix), "[%7.3f] [%-8s] [%-18s] ",
            entry.secondsSinceStart, DebugManager::LevelName(entry.level), entry.category.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, LogLevelColor(entry.level));
        ImGui::TextUnformatted(prefix);
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextUnformatted(entry.message.c_str());
        ImGui::PopStyleColor();
    }
    if (m_LogAutoScroll && wasAtBottom) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void UISystem::RenderDiagnosticsPhysics() {
    auto& physx = PhysXManager::GetInstance();
    ImGui::Text("Rigid actors: %u", physx.GetRigidActorCount());
    ImGui::Text("PVD status: %s", physx.GetPvdStatus().c_str());

    bool pvdConnected = physx.IsPvdConnected();
    if (ImGui::Checkbox("Connect PVD (127.0.0.1:5425)", &pvdConnected)) {
        if (pvdConnected) {
            physx.ConnectPvd();
        } else {
            physx.DisconnectPvd();
        }
    }
    ImGui::TextWrapped("PVD is an external NVIDIA application. Start its receiver first, then enable this connection.");

    bool contactLogging = physx.IsContactLoggingEnabled();
    if (ImGui::Checkbox("Log detailed contact samples", &contactLogging)) {
        physx.SetContactLoggingEnabled(contactLogging);
    }
    ImGui::TextWrapped("Contact metrics are always collected. Detailed samples are throttled to four per second.");

    ImGui::SeparatorText("Built-in Visualization");
    bool debugGeometry = physx.IsDebugVisualizationEnabled();
    if (ImGui::Checkbox("Collect PhysX debug geometry", &debugGeometry)) {
        physx.SetDebugVisualizationEnabled(debugGeometry);
    }
    ImGui::Text("Lines: %u", physx.GetDebugLineCount());
    ImGui::SameLine(180.0f);
    ImGui::Text("Triangles: %u", physx.GetDebugTriangleCount());
    ImGui::TextWrapped("The PhysX Space window renders these primitives with an independent debug camera.");

    bool showPhysXSpace = m_ShowPhysXSpace;
    if (ImGui::Checkbox("Show PhysX Space window", &showPhysXSpace)) {
        m_ShowPhysXSpace = showPhysXSpace;
        if (m_ShowPhysXSpace && !debugGeometry) {
            physx.SetDebugVisualizationEnabled(true);
        }
    }
}

void UISystem::RenderPhysXSpaceWindow() {
    if (!m_PhysXDebugRenderer) {
        m_ShowPhysXSpace = false;
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(720.0f, 480.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("PhysX Space", &m_ShowPhysXSpace)) {
        ImGui::End();
        return;
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const unsigned int width = static_cast<unsigned int>((std::max)(available.x, 64.0f));
    const unsigned int height = static_cast<unsigned int>((std::max)(available.y - 24.0f, 64.0f));
    auto& physx = PhysXManager::GetInstance();
    const auto* renderBuffer = physx.GetDebugRenderBuffer();
    if (renderBuffer && m_PhysXDebugRenderer->Render(m_Context, *renderBuffer, width, height)) {
        ImGui::Image(
            reinterpret_cast<ImTextureID>(m_PhysXDebugRenderer->GetTexture()),
            ImVec2(static_cast<float>(width), static_cast<float>(height)));
    } else {
        ImGui::TextUnformatted("No PhysX debug geometry is available yet.");
    }
    ImGui::Text("%u lines, %u triangles", physx.GetDebugLineCount(), physx.GetDebugTriangleCount());
    ImGui::End();
}

void UISystem::RenderWelcomeScreen() {
    ImGuiIO& io = ImGui::GetIO();
    
    // 创建全屏窗口
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, m_WelcomeAlpha));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    ImGui::Begin("WelcomeScreen", nullptr, 
        ImGuiWindowFlags_NoDecoration | 
        ImGuiWindowFlags_NoMove | 
        ImGuiWindowFlags_NoResize | 
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // 计算图片居中位置
    float windowWidth = io.DisplaySize.x;
    float windowHeight = io.DisplaySize.y;
    
    // 计算缩放比例以适应屏幕（保持宽高比）
    float scaleX = windowWidth * 0.6f / m_WelcomeImageWidth;   // 使用60%的屏幕宽度
    float scaleY = windowHeight * 0.6f / m_WelcomeImageHeight;
    float scale = (scaleX < scaleY) ? scaleX : scaleY;
    
    float displayWidth = m_WelcomeImageWidth * scale;
    float displayHeight = m_WelcomeImageHeight * scale;
    
    float posX = (windowWidth - displayWidth) * 0.5f;
    float posY = (windowHeight - displayHeight) * 0.5f;

    // 绘制图片
    ImGui::SetCursorPos(ImVec2(posX, posY));
    ImGui::Image(
        (ImTextureID)m_WelcomeTexture, 
        ImVec2(displayWidth, displayHeight),
        ImVec2(0, 0), 
        ImVec2(1, 1)
    );

    // 如果等待按键，显示提示文字
    if (m_WaitingForKeyPress && m_WelcomeScreenState == WelcomeScreenState::Display) {
        ImGui::SetCursorPos(ImVec2(windowWidth * 0.5f - 150, windowHeight - 100));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, m_WelcomeAlpha));
        ImGui::Text("Press any key to continue...");
        ImGui::PopStyleColor();
    }

    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void UISystem::ShowWelcomeScreen(const std::string& imagePath, float displayDuration) {
    // 加载纹理
    if (!LoadTextureFromFile(imagePath, &m_WelcomeTexture, &m_WelcomeImageWidth, &m_WelcomeImageHeight)) {
        std::cerr << "Failed to load welcome screen image: " << imagePath << std::endl;
        return;
    }

    m_WelcomeDisplayDuration = displayDuration;
    m_WelcomeTimer = 0.0f;
    m_WelcomeAlpha = 0.0f;
    m_WelcomeScreenState = WelcomeScreenState::FadeIn;
    m_WaitingForKeyPress = false;
    m_KeyPressed = false;

    std::cout << "Welcome screen started: " << imagePath << std::endl;
}

void UISystem::ShowWelcomeScreenWithKeyWait(const std::string& imagePath) {
    // 加载纹理
    if (!LoadTextureFromFile(imagePath, &m_WelcomeTexture, &m_WelcomeImageWidth, &m_WelcomeImageHeight)) {
        std::cerr << "Failed to load welcome screen image: " << imagePath << std::endl;
        return;
    }

    m_WelcomeTimer = 0.0f;
    m_WelcomeAlpha = 0.0f;
    m_WelcomeScreenState = WelcomeScreenState::FadeIn;
    m_WaitingForKeyPress = true;
    m_KeyPressed = false;

    std::cout << "Welcome screen started with key wait: " << imagePath << std::endl;
}

void UISystem::HideWelcomeScreen() {
    if (m_WelcomeScreenState != WelcomeScreenState::Hidden) {
        m_WelcomeTimer = 0.0f;
        m_WelcomeScreenState = WelcomeScreenState::FadeOut;
    }
}

bool UISystem::LoadTextureFromFile(const std::string& filename, ID3D11ShaderResourceView** outSRV, int* outWidth, int* outHeight) {
    // 使用stb_image加载图片
    int width, height, channels;
    unsigned char* imageData = stbi_load(filename.c_str(), &width, &height, &channels, 4); // 强制RGBA
    
    if (!imageData) {
        std::cerr << "Failed to load image: " << filename << std::endl;
        return false;
    }

    // 创建D3D11纹理
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = imageData;
    initData.SysMemPitch = width * 4;

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = m_Device->CreateTexture2D(&texDesc, &initData, &texture);
    
    stbi_image_free(imageData); // 释放stb_image的内存

    if (FAILED(hr)) {
        std::cerr << "Failed to create D3D11 texture" << std::endl;
        return false;
    }

    // 创建Shader Resource View
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = m_Device->CreateShaderResourceView(texture, &srvDesc, outSRV);
    texture->Release();

    if (FAILED(hr)) {
        std::cerr << "Failed to create shader resource view" << std::endl;
        return false;
    }

    *outWidth = width;
    *outHeight = height;

    std::cout << "Loaded texture: " << filename << " (" << width << "x" << height << ")" << std::endl;

    return true;
}

void UISystem::Shutdown() {
    if (m_PhysXDebugRenderer) {
        m_PhysXDebugRenderer->Shutdown();
        m_PhysXDebugRenderer.reset();
    }

    if (m_WelcomeTexture) {
        m_WelcomeTexture->Release();
        m_WelcomeTexture = nullptr;
    }

    if (m_ImGuiInitialized) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_ImGuiInitialized = false;
    }
}

} // namespace outer_wilds
