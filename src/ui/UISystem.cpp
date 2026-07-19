#include "UISystem.h"
#include "../core/DebugManager.h"
#include "../core/ProjectPaths.h"
#include "../core/TimeManager.h"
#include "../physics/PhysXManager.h"
#include "../gameplay/components/GameState.h"
#include "../gameplay/components/ObjectiveComponent.h"
#include "../gameplay/components/OrbitNavigationComponent.h"
#include "../gameplay/components/NavigationTargetState.h"
#include "../gameplay/components/SpacecraftComponent.h"
#include "../gameplay/components/SolarMapState.h"
#include "../physics/components/SectorComponent.h"
#include "../input/InputManager.h"
#include "../graphics/debug/PhysXDebugRenderer.h"
#include "../graphics/components/CameraComponent.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
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

    if (m_PendingMapLockTarget != entt::null) {
        if (!registry.ctx().contains<components::NavigationTargetState>()) {
            registry.ctx().emplace<components::NavigationTargetState>();
        }
        auto& target = registry.ctx().get<components::NavigationTargetState>();
        target.lockedTarget = m_PendingMapLockTarget;
        target.matchCompletionLogged = false;
        m_PendingMapLockTarget = entt::null;
    }
    UpdateGameplaySnapshot(registry);

    if (InputManager::GetInstance().IsKeyPressed('M') &&
        m_WelcomeScreenState == WelcomeScreenState::Hidden) {
        m_ShowNavigationMap = !m_ShowNavigationMap;
        if (m_ShowNavigationMap) {
            m_MouseLookBeforeMap = InputManager::GetInstance().IsMouseLookEnabled();
            InputManager::GetInstance().SetMouseLookEnabled(false);
        } else {
            InputManager::GetInstance().SetMouseLookEnabled(m_MouseLookBeforeMap);
        }
    }

    if (!registry.ctx().contains<components::SolarMapViewState>()) {
        registry.ctx().emplace<components::SolarMapViewState>();
    }
    auto& mapView = registry.ctx().get<components::SolarMapViewState>();
    if (m_ShowNavigationMap && InputManager::GetInstance().IsKeyPressed(VK_TAB)) {
        m_MapCenteredOnSpacecraft = !m_MapCenteredOnSpacecraft;
        m_FocusedMapBody = entt::null;
        m_MapPan = {0.0f, 0.0f};
    }
    mapView.requestedOpen = m_ShowNavigationMap;
    mapView.yaw = m_MapYaw;
    mapView.pitch = m_MapPitch;
    mapView.zoom = m_MapZoom;
    mapView.centerOnSpacecraft = m_MapCenteredOnSpacecraft;
    mapView.focusedBody = m_FocusedMapBody;

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

    if (m_WelcomeScreenState == WelcomeScreenState::Hidden && !m_ShowNavigationMap) {
        RenderObjectiveHud();
        RenderNavigationTargetHud();
    }

    if (m_ShowNavigationMap) RenderNavigationMap();

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

void UISystem::UpdateGameplaySnapshot(entt::registry& registry) {
    m_ObjectiveHud = {};
    m_Navigation = {};
    m_NavigationTarget = {};

    auto navigationView = registry.view<components::OrbitNavigationComponent, components::SpacecraftComponent>();
    entt::entity navigationEntity = entt::null;
    for (const auto entity : navigationView) {
        navigationEntity = entity;
        if (navigationView.get<components::SpacecraftComponent>(entity).currentState ==
            components::SpacecraftComponent::State::PILOTED) {
            break;
        }
    }
    if (navigationEntity != entt::null) {
        const auto& navigation = navigationView.get<components::OrbitNavigationComponent>(navigationEntity);
        if (!navigation.predictedTrajectory.empty()) {
            m_Navigation.available = true;
            m_Navigation.bodyRadius = navigation.bodyRadius;
            m_Navigation.altitude = navigation.altitude;
            m_Navigation.speed = navigation.speed;
            m_Navigation.radialSpeed = navigation.radialSpeed;
            m_Navigation.tangentialSpeed = navigation.tangentialSpeed;
            m_Navigation.circularOrbitSpeed = navigation.circularOrbitSpeed;
            m_Navigation.circularizeActive = navigation.circularizeActive;
            m_Navigation.periapsisAltitude = navigation.predictedPeriapsisAltitude;
            m_Navigation.apoapsisAltitude = navigation.predictedApoapsisAltitude;
            m_Navigation.trajectoryStatus = navigation.circularizeActive
                ? "CIRCULARIZING"
                : (navigation.predictedImpact
                    ? "IMPACT"
                    : (navigation.predictedEscape
                        ? "ESCAPE"
                        : (navigation.stableOrbit ? "STABLE" : "BALLISTIC")));
            if (const auto* sector = registry.try_get<components::SectorComponent>(navigation.centralBody)) {
                m_Navigation.bodyName = sector->name;
            }

        }
    }

    if (const auto* solarMap = registry.ctx().find<components::SolarMapState>()) {
        m_Navigation.bodies.reserve(solarMap->bodies.size());
        for (const auto& body : solarMap->bodies) {
            m_Navigation.bodies.push_back({
                body.entity, body.name, body.position, body.radius, body.currentSector});
        }
        m_Navigation.bodyOrbits.reserve(solarMap->bodyOrbits.size());
        for (const auto& orbit : solarMap->bodyOrbits) {
            m_Navigation.bodyOrbits.push_back({orbit.points, false});
        }
        m_Navigation.predictedWorldTrajectory = {
            solarMap->predictedSpacecraftPath.points,
            solarMap->predictedSpacecraftPath.dashed};
        m_Navigation.spacecraftWorldPosition = solarMap->spacecraftPosition;
        m_Navigation.hasSpacecraftWorldPosition = solarMap->hasSpacecraft;
        m_Navigation.available = !m_Navigation.bodies.empty();
    }
    if (const auto* mapView = registry.ctx().find<components::SolarMapViewState>()) {
        m_Navigation.viewProjection = mapView->viewProjection;
        m_Navigation.hasMapCamera = mapView->cameraOverrideActive;
        m_Navigation.cameraTransition = mapView->transition;
    }
    if (const auto* target = registry.ctx().find<components::NavigationTargetState>()) {
        m_Navigation.lockedTarget = target->lockedTarget;
        m_NavigationTarget.hasCandidate = target->hasCandidate;
        m_NavigationTarget.hasLockedTarget = target->lockedTarget != entt::null;
        m_NavigationTarget.matchingVelocity = target->matchingVelocity;
        m_NavigationTarget.candidateWorldPosition = target->candidateWorldPosition;
        m_NavigationTarget.targetWorldPosition = target->targetWorldPosition;
        m_NavigationTarget.targetName = target->targetName;
        m_NavigationTarget.distance = target->distance;
        m_NavigationTarget.relativeSpeed = target->relativeSpeed;

        auto cameraView = registry.view<components::CameraComponent>();
        for (const auto entity : cameraView) {
            const auto& camera = cameraView.get<components::CameraComponent>(entity);
            if (!camera.isActive) continue;
            const auto view = DirectX::XMMatrixLookAtLH(
                DirectX::XMLoadFloat3(&camera.position),
                DirectX::XMLoadFloat3(&camera.target),
                DirectX::XMLoadFloat3(&camera.up));
            const auto projection = DirectX::XMMatrixPerspectiveFovLH(
                DirectX::XMConvertToRadians(camera.fov),
                camera.aspectRatio,
                camera.nearPlane,
                camera.farPlane);
            DirectX::XMStoreFloat4x4(&m_NavigationTarget.viewProjection, view * projection);
        }
    }

    const auto* gameState = registry.ctx().find<components::GameState>();
    if (!gameState || gameState->activeObjective == entt::null ||
        !registry.valid(gameState->activeObjective)) {
        return;
    }

    const auto* objective = registry.try_get<components::ObjectiveComponent>(gameState->activeObjective);
    if (!objective || !objective->showInHud ||
        objective->status != components::ObjectiveStatus::Active) {
        return;
    }

    m_ObjectiveHud.visible = true;
    m_ObjectiveHud.title = objective->title;
    m_ObjectiveHud.description = objective->description;
    m_ObjectiveHud.progress = objective->progress;
    m_ObjectiveHud.requiredProgress = objective->requiredProgress;
}

void UISystem::RenderNavigationMap() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.01f, 0.018f, 0.022f, 0.12f));
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
    ImGui::Begin("NavigationMap", nullptr, flags);

    auto* drawList = ImGui::GetWindowDrawList();
    if (ImGui::IsWindowHovered()) {
        const bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        if (io.MouseWheel != 0.0f) {
            m_MapZoom = std::clamp(m_MapZoom * std::pow(1.16f, io.MouseWheel), 0.25f, 16.0f);
        }
        if (leftDown && !rightDown && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            m_MapYaw += io.MouseDelta.x * 0.006f;
            m_MapPitch = std::clamp(m_MapPitch + io.MouseDelta.y * 0.006f, -1.45f, 1.45f);
        }
        if (rightDown && !leftDown && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.0f)) {
            m_MapPan.x += io.MouseDelta.x;
            m_MapPan.y += io.MouseDelta.y;
        }
    }

    if (m_Navigation.available && !m_Navigation.bodies.empty()) {
        auto project = [&](const DirectX::XMFLOAT3& point, float* depth = nullptr) {
            const auto projected = DirectX::XMVector3TransformCoord(
                DirectX::XMLoadFloat3(&point),
                DirectX::XMLoadFloat4x4(&m_Navigation.viewProjection));
            DirectX::XMFLOAT3 ndc;
            DirectX::XMStoreFloat3(&ndc, projected);
            if (depth) *depth = ndc.z;
            return ImVec2(
                (ndc.x * 0.5f + 0.5f) * io.DisplaySize.x + m_MapPan.x,
                (-ndc.y * 0.5f + 0.5f) * io.DisplaySize.y + m_MapPan.y);
        };

        const ImU32 orbitColor = IM_COL32(111, 130, 136, 105);
        for (const auto& orbit : m_Navigation.bodyOrbits) {
            for (std::size_t index = 1; index < orbit.points.size(); ++index) {
                drawList->AddLine(project(orbit.points[index - 1]), project(orbit.points[index]), orbitColor, 1.0f);
            }
        }

        const ImU32 predictionColor = m_Navigation.trajectoryStatus == "IMPACT"
            ? IM_COL32(244, 97, 74, 245)
            : (m_Navigation.trajectoryStatus == "ESCAPE"
                ? IM_COL32(244, 180, 73, 245)
                : IM_COL32(93, 224, 200, 245));
        const auto& predicted = m_Navigation.predictedWorldTrajectory.points;
        for (std::size_t index = 1; index < predicted.size(); ++index) {
            if ((index / 3) % 2 == 0) {
                drawList->AddLine(project(predicted[index - 1]), project(predicted[index]), predictionColor, 2.5f);
            }
        }

        struct ProjectedBody {
            const NavigationSnapshot::Body* body = nullptr;
            ImVec2 screen;
            float depth = 0.0f;
            float displayRadius = 5.0f;
        };
        std::vector<ProjectedBody> projectedBodies;
        projectedBodies.reserve(m_Navigation.bodies.size());
        for (const auto& body : m_Navigation.bodies) {
            float depth = 0.0f;
            const ImVec2 screen = project(body.position, &depth);
            const float radius = body.name.find("Sun") != std::string::npos ? 14.0f : 7.0f;
            projectedBodies.push_back({&body, screen, depth, radius});
        }
        std::sort(projectedBodies.begin(), projectedBodies.end(), [](const auto& a, const auto& b) {
            return a.depth > b.depth;
        });

        const ImVec2 mouse = io.MousePos;
        ProjectedBody* hovered = nullptr;
        float hoverDistance = (std::numeric_limits<float>::max)();
        for (auto& projected : projectedBodies) {
            const float dx = mouse.x - projected.screen.x;
            const float dy = mouse.y - projected.screen.y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance < (std::max)(projected.displayRadius + 7.0f, 14.0f) && distance < hoverDistance) {
                hovered = &projected;
                hoverDistance = distance;
            }
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            m_SelectedMapBody = hovered->body->entity;
            m_PendingMapLockTarget = hovered->body->entity;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                m_FocusedMapBody = hovered->body->entity;
                m_MapPan = {0.0f, 0.0f};
            }
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Right)) {
            m_FocusedMapBody = entt::null;
            m_MapPan = {0.0f, 0.0f};
            m_MapZoom = 1.0f;
        }

        bool selectedStillExists = false;
        for (const auto& projected : projectedBodies) {
            const bool isSelected = projected.body->entity == m_SelectedMapBody;
            const bool isLocked = projected.body->entity == m_Navigation.lockedTarget;
            const bool isHovered = hovered && hovered->body->entity == projected.body->entity;
            selectedStillExists = selectedStillExists || isSelected;
            ImU32 bodyColor = projected.body->name.find("Sun") != std::string::npos
                ? IM_COL32(247, 184, 80, 210)
                : IM_COL32(142, 205, 216, 210);
            if (projected.body->currentSector) bodyColor = IM_COL32(92, 224, 196, 240);
            drawList->AddCircle(projected.screen, projected.displayRadius, bodyColor, 24, 1.5f);
            if (isSelected || isHovered || projected.body->currentSector) {
                drawList->AddCircle(
                    projected.screen,
                    projected.displayRadius + (isSelected ? 6.0f : 3.0f),
                    isSelected ? IM_COL32(255, 207, 91, 255) : IM_COL32(205, 232, 232, 220),
                    28, isSelected ? 2.0f : 1.0f);
            }
            if (isLocked) {
                drawList->AddCircle(
                    projected.screen, projected.displayRadius + 10.0f,
                    IM_COL32(93, 224, 200, 255), 4, 2.0f);
            }
            const ImVec2 labelSize = ImGui::CalcTextSize(projected.body->name.c_str());
            drawList->AddText(
                ImVec2(projected.screen.x - labelSize.x * 0.5f,
                       projected.screen.y + projected.displayRadius + 6.0f),
                IM_COL32(218, 226, 226, 220), projected.body->name.c_str());
        }
        if (!selectedStillExists) m_SelectedMapBody = entt::null;

        if (m_Navigation.hasSpacecraftWorldPosition) {
            const ImVec2 ship = project(m_Navigation.spacecraftWorldPosition);
            drawList->AddTriangleFilled(
                ImVec2(ship.x, ship.y - 7.0f),
                ImVec2(ship.x - 5.0f, ship.y + 5.0f),
                ImVec2(ship.x + 5.0f, ship.y + 5.0f),
                IM_COL32(255, 210, 92, 255));
        }

        const NavigationSnapshot::Body* selectedBody = nullptr;
        for (const auto& body : m_Navigation.bodies) {
            if (body.entity == m_SelectedMapBody) selectedBody = &body;
        }

        ImGui::SetCursorPos(ImVec2(32.0f, 30.0f));
        ImGui::BeginGroup();
        ImGui::TextUnformatted("SOLAR SYSTEM");
        ImGui::PushStyleColor(ImGuiCol_Text, predictionColor);
        ImGui::TextUnformatted(m_Navigation.trajectoryStatus.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
        if (selectedBody) {
            ImGui::TextUnformatted(selectedBody->name.c_str());
            if (m_Navigation.hasSpacecraftWorldPosition) {
                const float dx = selectedBody->position.x - m_Navigation.spacecraftWorldPosition.x;
                const float dy = selectedBody->position.y - m_Navigation.spacecraftWorldPosition.y;
                const float dz = selectedBody->position.z - m_Navigation.spacecraftWorldPosition.z;
                ImGui::Text("DISTANCE       %7.1f m", std::sqrt(dx * dx + dy * dy + dz * dz));
            }
        } else {
            ImGui::Text("ALTITUDE       %7.1f m", m_Navigation.altitude);
            ImGui::Text("SPEED          %7.1f m/s", m_Navigation.speed);
            ImGui::Text("RADIAL         %+7.1f m/s", m_Navigation.radialSpeed);
            ImGui::Text("TANGENTIAL     %7.1f m/s", m_Navigation.tangentialSpeed);
            ImGui::Text("CIRCULAR       %7.1f m/s", m_Navigation.circularOrbitSpeed);
            ImGui::Text("PERIAPSIS      %7.1f m", m_Navigation.periapsisAltitude);
            ImGui::Text("APOAPSIS       %7.1f m", m_Navigation.apoapsisAltitude);
        }
        ImGui::EndGroup();
    } else {
        const char* unavailable = "NO LOCAL ORBIT DATA";
        const ImVec2 unavailableCenter(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        const ImVec2 textSize = ImGui::CalcTextSize(unavailable);
        ImGui::SetCursorPos(ImVec2(unavailableCenter.x - textSize.x * 0.5f, unavailableCenter.y));
        ImGui::TextUnformatted(unavailable);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void UISystem::RenderNavigationTargetHud() {
    if (!m_NavigationTarget.hasCandidate && !m_NavigationTarget.hasLockedTarget) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoBackground;
    ImGui::Begin("NavigationTargetHud", nullptr, flags);
    auto* drawList = ImGui::GetWindowDrawList();

    auto project = [&](const DirectX::XMFLOAT3& point, ImVec2& screen) {
        const auto projected = DirectX::XMVector3TransformCoord(
            DirectX::XMLoadFloat3(&point),
            DirectX::XMLoadFloat4x4(&m_NavigationTarget.viewProjection));
        DirectX::XMFLOAT3 ndc;
        DirectX::XMStoreFloat3(&ndc, projected);
        if (ndc.z <= 0.0f || ndc.z >= 1.0f) return false;
        screen = {
            (ndc.x * 0.5f + 0.5f) * io.DisplaySize.x,
            (-ndc.y * 0.5f + 0.5f) * io.DisplaySize.y};
        return screen.x >= -80.0f && screen.x <= io.DisplaySize.x + 80.0f &&
            screen.y >= -80.0f && screen.y <= io.DisplaySize.y + 80.0f;
    };

    auto drawBrackets = [&](const ImVec2& center, float radius, ImU32 color, bool doubled) {
        const float arm = 9.0f;
        auto drawPair = [&](float inset) {
            const float left = center.x - radius - inset;
            const float right = center.x + radius + inset;
            const float top = center.y - radius;
            const float bottom = center.y + radius;
            drawList->AddLine({left, top}, {left, bottom}, color, 2.0f);
            drawList->AddLine({left, top}, {left + arm, top}, color, 2.0f);
            drawList->AddLine({left, bottom}, {left + arm, bottom}, color, 2.0f);
            drawList->AddLine({right, top}, {right, bottom}, color, 2.0f);
            drawList->AddLine({right - arm, top}, {right, top}, color, 2.0f);
            drawList->AddLine({right - arm, bottom}, {right, bottom}, color, 2.0f);
        };
        drawPair(0.0f);
        if (doubled) drawPair(6.0f);
    };

    if (m_NavigationTarget.hasCandidate && !m_NavigationTarget.hasLockedTarget) {
        ImVec2 candidate;
        if (project(m_NavigationTarget.candidateWorldPosition, candidate)) {
            drawBrackets(candidate, 20.0f, IM_COL32(190, 210, 212, 150), false);
        }
    }

    if (m_NavigationTarget.hasLockedTarget) {
        ImVec2 target;
        if (project(m_NavigationTarget.targetWorldPosition, target)) {
            const ImU32 color = m_NavigationTarget.matchingVelocity
                ? IM_COL32(255, 205, 92, 255)
                : IM_COL32(93, 224, 200, 255);
            drawBrackets(target, 28.0f, color, true);
            const std::string label = m_NavigationTarget.targetName + "  " +
                std::to_string(static_cast<int>(m_NavigationTarget.distance)) + " m";
            const std::string velocity =
                (m_NavigationTarget.matchingVelocity ? "MATCHING  " : "REL  ") +
                std::to_string(static_cast<int>(m_NavigationTarget.relativeSpeed)) + " m/s";
            const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
            const ImVec2 velocitySize = ImGui::CalcTextSize(velocity.c_str());
            drawList->AddText({target.x - labelSize.x * 0.5f, target.y + 38.0f}, color, label.c_str());
            drawList->AddText(
                {target.x - velocitySize.x * 0.5f, target.y + 55.0f}, color, velocity.c_str());
        }
    }

    ImGui::End();
}

void UISystem::RenderObjectiveHud() {
    if (!m_ObjectiveHud.visible) return;

    ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(300.0f, 0.0f), ImVec2(420.0f, 180.0f));
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
    if (ImGui::Begin("ObjectiveHud", nullptr, flags)) {
        ImGui::TextUnformatted(m_ObjectiveHud.title.c_str());
        if (!m_ObjectiveHud.description.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.76f, 0.80f, 1.0f));
            ImGui::TextWrapped("%s", m_ObjectiveHud.description.c_str());
            ImGui::PopStyleColor();
        }
        const float fraction = m_ObjectiveHud.requiredProgress > 0.0f
            ? m_ObjectiveHud.progress / m_ObjectiveHud.requiredProgress
            : 0.0f;
        ImGui::ProgressBar((std::clamp)(fraction, 0.0f, 1.0f), ImVec2(-1.0f, 4.0f), "");
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
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
