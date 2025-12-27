#include "core/Engine.h"
#include "core/DebugManager.h"
#include "audio/AudioSystem.h"
#include "ui/UISystem.h"
#include "scene/Scene.h"
#include "scene/SceneAssetLoader.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include "scene/components/TransformComponent.h"
#include "graphics/components/RenderableComponent.h"
#include "graphics/components/RenderPriorityComponent.h"
#include "graphics/components/CameraComponent.h"
#include "graphics/components/FreeCameraComponent.h"
#include "graphics/components/MeshComponent.h"
#include "gameplay/components/PlayerInputComponent.h"
#include "gameplay/components/PlayerComponent.h"
#include "gameplay/components/PlayerAlignmentComponent.h"
#include "gameplay/components/CharacterControllerComponent.h"
#include "gameplay/components/OrbitComponent.h"
#include "gameplay/components/SpacecraftComponent.h"
#include "physics/components/GravitySourceComponent.h"
#include "physics/components/GravityAffectedComponent.h"
#include "physics/components/RigidBodyComponent.h"
#include "physics/components/GravityAlignmentComponent.h"
#include "physics/components/SectorComponent.h"
#include "graphics/resources/Material.h"
#include "graphics/resources/Shader.h"
#include "graphics/resources/TextureLoader.h"
#include "graphics/resources/AssimpLoader.h"
#include "graphics/RenderSystem.h"
#include "physics/PhysXManager.h"
#include <PxPhysicsAPI.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <Windows.h>
#include <iostream>
#include <sstream>

// Forward declare ImGui's Win32 message handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Function to create and redirect IO to a console
void CreateDebugConsole() {
    if (AllocConsole()) {
        FILE* pCout;
        freopen_s(&pCout, "CONOUT$", "w", stdout);
        FILE* pCerr;
        freopen_s(&pCerr, "CONOUT$", "w", stderr);
        FILE* pCin;
        freopen_s(&pCin, "CONIN$", "r", stdin);

        std::cout.clear();
        std::cerr.clear();
        std::cin.clear();
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Let ImGui handle the message first
    if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam))
        return true;
    
    switch (uMsg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_CLOSE:
            outer_wilds::Engine::GetInstance().Stop();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

HWND CreateAppWindow(HINSTANCE hInstance, int width, int height) {
    const char CLASS_NAME[] = "OuterWildsECS Window Class";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, "Outer Wilds ECS",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        NULL, NULL, hInstance, NULL
    );

    return hwnd;
}


int main(int argc, char* argv[]) {
    CreateDebugConsole();
    outer_wilds::DebugManager::GetInstance().Log("Main", "Debug Console Initialized.");

    const int WINDOW_WIDTH = 1280;
    const int WINDOW_HEIGHT = 720;

    HINSTANCE hInstance = GetModuleHandle(NULL);

    outer_wilds::DebugManager::GetInstance().Log("Main", "Creating window...");

    HWND hwnd = CreateAppWindow(hInstance, WINDOW_WIDTH, WINDOW_HEIGHT);
    if (hwnd == NULL) {
        outer_wilds::DebugManager::GetInstance().Log("Main", "Failed to create window.");
        return 0;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd); // Force window to update
    SetForegroundWindow(hwnd); // Bring window to front

    // Debug: Log window info
    RECT rect;
    GetWindowRect(hwnd, &rect);
    outer_wilds::DebugManager::GetInstance().Log("Main", "Window created at (" + std::to_string(rect.left) + "," + std::to_string(rect.top) + ") size " + std::to_string(rect.right - rect.left) + "x" + std::to_string(rect.bottom - rect.top));
    outer_wilds::DebugManager::GetInstance().Log("Main", "Window is visible: " + std::to_string(IsWindowVisible(hwnd)));

    // Initialize engine
    outer_wilds::Engine& engine = outer_wilds::Engine::GetInstance();
    
    if (engine.Initialize(hwnd, WINDOW_WIDTH, WINDOW_HEIGHT)) {
        outer_wilds::DebugManager::GetInstance().Log("Main", "Engine initialized successfully");
        
        // 启用FPS显示
        outer_wilds::DebugManager::GetInstance().SetShowFPS(true);
        
        // Get the default scene
        auto scene = engine.GetSceneManager()->GetActiveScene();

        // Create ground physics material with friction
        auto& physxManager = outer_wilds::PhysXManager::GetInstance();
        physx::PxMaterial* groundPhysicsMaterial = physxManager.GetPhysics()->createMaterial(0.8f, 0.6f, 0.1f); // static, dynamic, restitution
        
        // Create a simple quad mesh for the ground plane (for rendering)
        auto groundMesh = std::make_shared<outer_wilds::resources::Mesh>();
        std::vector<outer_wilds::resources::Vertex> groundVertices;
        std::vector<uint32_t> groundIndices;
        
        // Create a large quad at Y=0 (local space)
        const float s = 100.0f; // Large ground plane
        groundVertices = {
            {{ -s, 0.0f, -s }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f }}, // bottom-left
            {{  s, 0.0f, -s }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f }}, // bottom-right
            {{  s, 0.0f,  s }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f }}, // top-right
            {{ -s, 0.0f,  s }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f }}  // top-left
        };
        
        // Triangle list indices (counter-clockwise winding for upward-facing normal)
        groundIndices = { 0, 2, 1, 0, 3, 2 };
        
        groundMesh->SetVertices(groundVertices);
        groundMesh->SetIndices(groundIndices);
        
        auto groundRenderMaterial = std::make_shared<outer_wilds::resources::Material>();
        groundRenderMaterial->albedo = { 1.0f, 1.0f, 1.0f, 1.0f }; // White color
        groundRenderMaterial->metallic = 0.0f;
        groundRenderMaterial->roughness = 0.5f;
        groundRenderMaterial->ao = 1.0f;
        groundRenderMaterial->isTransparent = false;
        
        // ============================================
        // Load texture for ground plane
        // ============================================
        // NOTE: Even though ground uses RenderableComponent (old system),
        // we can still apply textures if the shader supports it
        // ============================================
        ID3D11ShaderResourceView* groundTextureSRV = nullptr;
        if (outer_wilds::resources::TextureLoader::LoadFromFile(
                engine.GetRenderSystem()->GetBackend()->GetDevice(),
                "assets/Texture/plastered_stone_wall_diff_2k.jpg",
                &groundTextureSRV)) {
            groundRenderMaterial->shaderProgram = groundTextureSRV;
            groundRenderMaterial->albedoTexture = "assets/Texture/plastered_stone_wall_diff_2k.jpg";
            outer_wilds::DebugManager::GetInstance().Log("Main", "Ground texture loaded successfully!");
        } else {
            outer_wilds::DebugManager::GetInstance().Log("Main", "Failed to load ground texture");
        }
        
        // ========================================
        // 地平面测试场景（暂时禁用）
        // ========================================
        // outer_wilds::DebugManager::GetInstance().Log("Main", "Creating ground plane test scene...");
        // 
        // // === 创建渲染用的地面实体（使用已创建的groundMesh）===
        // auto groundEntity = scene->CreateEntity("ground_render");
        // auto& groundEntityTransform = scene->GetRegistry().emplace<outer_wilds::TransformComponent>(groundEntity);
        // groundEntityTransform.position = {0.0f, 0.0f, 0.0f};
        // groundEntityTransform.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        // groundEntityTransform.scale = {1.0f, 1.0f, 1.0f};  // groundMesh已经是100x100米
        // 
        // auto& groundMeshComponent = scene->GetRegistry().emplace<outer_wilds::components::MeshComponent>(groundEntity);
        // groundMeshComponent.mesh = groundMesh;
        // groundMeshComponent.material = groundRenderMaterial;
        // groundMeshComponent.isVisible = true;
        // groundMeshComponent.castsShadows = false;
        // 
        // auto& groundRenderPriority = scene->GetRegistry().emplace<outer_wilds::components::RenderPriorityComponent>(groundEntity);
        // groundRenderPriority.renderPass = 0;  // Opaque pass
        // groundRenderPriority.sortKey = 100;
        // groundRenderPriority.lodLevel = 0;
        // 
        // // 创建PhysX静态地面碰撞体（Box而不是Plane，更好控制）
        // physx::PxPhysics* physics = physxManager.GetPhysics();
        // physx::PxShape* groundShape = physics->createShape(
        //     physx::PxBoxGeometry(50.0f, 0.5f, 50.0f),  // 100x1x100米的盒子
        //     *groundPhysicsMaterial,
        //     true
        // );
        // 
        // physx::PxTransform groundPose(physx::PxVec3(0.0f, -0.5f, 0.0f));  // 地面在Y=-0.5
        // physx::PxRigidStatic* groundActor = physics->createRigidStatic(groundPose);
        // groundActor->attachShape(*groundShape);
        // groundShape->release();
        // 
        // physx::PxScene* pxScene = physxManager.GetScene();
        // pxScene->addActor(*groundActor);
        // 
        // outer_wilds::DebugManager::GetInstance().Log("Main", "Ground plane created!");");
        // 
        // // ========================================
        // // 创建玩家（地平面测试）
        // // ========================================
        // outer_wilds::DebugManager::GetInstance().Log("Main", "=== Creating Player ===");
        // 
        // auto playerEntity = scene->CreateEntity("player");
        // auto& playerTransform = scene->GetRegistry().emplace<outer_wilds::TransformComponent>(playerEntity);
        // playerTransform.position = {0.0f, 2.0f, 0.0f};  // 地面上方2米
        // playerTransform.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        // 
        // outer_wilds::DebugManager::GetInstance().Log("Main", "Player spawn at (0, 2, 0) - 2m above ground");
        // 
        // // Add player component
        // scene->GetRegistry().emplace<outer_wilds::PlayerComponent>(playerEntity);
        // 
        // // 地平面测试不需要重力影响组件响组件
        
        // ========================================
        // 星球重力测试场景
        // ========================================
        outer_wilds::DebugManager::GetInstance().Log("Main", "=== Creating Planet Gravity Test Scene ===");
        
        // 星球参数(简单明了)
        const float PLANET_SCALE = 10.0f;           // 星球缩放倍数
        const float PLANET_MODEL_RADIUS = 6.4f;     // 原始obj模型的实际半径(米) - 从OBJ顶点测量
        const float PLANET_ACTUAL_RADIUS = PLANET_SCALE * PLANET_MODEL_RADIUS;  // 实际半径 = 64米

        // 玩家胶囊体参数（保持与 PhysX 控制器一致）
        const float CONTROLLER_HEIGHT = 1.6f;
        const float CONTROLLER_RADIUS = 0.4f;
        const float CONTROLLER_CONTACT_OFFSET = 0.1f;

        // 玩家脚底距离星球中心 = 星球半径 + 胶囊半高 + 安全余量
        const float CAPSULE_HALF_HEIGHT = (CONTROLLER_HEIGHT * 0.5f) + CONTROLLER_RADIUS; // 半高(包含半径)
        const float PLAYER_SURFACE_PADDING = 0.5f;  // 额外抬离表面，避免初始嵌入
        const float PLAYER_START_HEIGHT = PLANET_ACTUAL_RADIUS + CAPSULE_HALF_HEIGHT + CONTROLLER_CONTACT_OFFSET + PLAYER_SURFACE_PADDING;
        
        std::stringstream configLog;
        configLog << "Planet Config: Scale=" << PLANET_SCALE 
                  << ", Actual Radius=" << PLANET_ACTUAL_RADIUS 
                  << "m, Player Height=" << PLAYER_START_HEIGHT << "m";
        outer_wilds::DebugManager::GetInstance().Log("Main", configLog.str());
        
        // 创建星球物理配置
        outer_wilds::PhysicsOptions planetPhysics;
        planetPhysics.addCollider = true;
        planetPhysics.addRigidBody = true;  // 重新启用地球物理
        // 【测试】改用 Box 碰撞体，测试是否是 Sphere-Sphere 碰撞 bug
        planetPhysics.shape = outer_wilds::PhysicsOptions::ColliderShape::Box;
        planetPhysics.boxExtent = DirectX::XMFLOAT3(
            PLANET_ACTUAL_RADIUS * 2.0f / PLANET_SCALE,  // 需要除以 scale，因为后面会乘
            PLANET_ACTUAL_RADIUS * 2.0f / PLANET_SCALE,
            PLANET_ACTUAL_RADIUS * 2.0f / PLANET_SCALE
        );  // 128m 边长的立方体
        // planetPhysics.shape = outer_wilds::PhysicsOptions::ColliderShape::Sphere;
        // planetPhysics.sphereRadius = PLANET_MODEL_RADIUS;  // 6.4米,会自动乘scale变成64米
        planetPhysics.mass = 0.0f;                         // 静态物体
        planetPhysics.useGravity = false;                  // 星球本身不受重力
        planetPhysics.isKinematic = true;
        planetPhysics.staticFriction = 0.8f;
        planetPhysics.dynamicFriction = 0.6f;
        planetPhysics.restitution = 0.2f;

        // 加载星球模型
        entt::entity planetEntity = outer_wilds::SceneAssetLoader::LoadModelAsEntity(
            scene->GetRegistry(),
            scene,
            engine.GetRenderSystem()->GetBackend()->GetDevice(),
            "assets/BlendObj/planet1.obj",
            "assets/Texture/plastered_stone_wall_diff_2k.jpg",
            DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),           // 星球中心在原点
            DirectX::XMFLOAT3(PLANET_SCALE, PLANET_SCALE, PLANET_SCALE),  // 缩放10倍
            &planetPhysics
        );

        if (planetEntity != entt::null) {
            // 添加重力源组件
            auto& gravitySource = scene->GetRegistry().emplace<outer_wilds::components::GravitySourceComponent>(planetEntity);
            gravitySource.radius = PLANET_ACTUAL_RADIUS;        // 64米
            gravitySource.surfaceGravity = 9.8f;
            gravitySource.atmosphereHeight = 20.0f;              // 大气层20米
            gravitySource.useRealisticGravity = false;
            gravitySource.isActive = true;
            
            std::stringstream gravLog;
            gravLog << "Planet gravity source: radius=" << gravitySource.radius 
                    << "m, influence=" << gravitySource.GetInfluenceRadius() << "m";
            outer_wilds::DebugManager::GetInstance().Log("Main", gravLog.str());
            
            // 验证物理Actor是否创建
            auto* rigidBody = scene->GetRegistry().try_get<outer_wilds::RigidBodyComponent>(planetEntity);
            if (rigidBody) {
                if (rigidBody->physxActor) {
                    outer_wilds::DebugManager::GetInstance().Log("Main", "Planet PhysX actor created successfully");
                } else {
                    outer_wilds::DebugManager::GetInstance().Log("Main", "Planet PhysX actor is NULL!");
                }
            } else {
                outer_wilds::DebugManager::GetInstance().Log("Main", "Planet has no RigidBodyComponent!");
            }
            
            // 【测试】添加轨道组件 - 启用公转
            auto& planetOrbit = scene->GetRegistry().emplace<outer_wilds::components::OrbitComponent>(planetEntity);
            planetOrbit.centerEntity = entt::null;  // 围绕世界原点
            planetOrbit.radius = 100.0f;  // 100米轨道半径
            planetOrbit.period = 60.0f;   // 60秒一圈（慢速，方便观察）
            planetOrbit.currentAngle = 0.0f;  // 从X轴正方向开始
            planetOrbit.isActive = true;  // 启用公转
            
            // 添加 Sector 组件（使地球成为一个局部坐标系）
            auto& earthSector = scene->GetRegistry().emplace<outer_wilds::components::SectorComponent>(planetEntity);
            earthSector.name = "Earth";
            earthSector.influenceRadius = 200.0f;  // 200米影响范围
            earthSector.priority = 10;  // 高优先级
            earthSector.isActive = true;  // 初始为活跃 Sector
            
            // 添加天体类型组件
            scene->GetRegistry().emplace<outer_wilds::components::SectorEntityTypeComponent>(
                planetEntity, outer_wilds::components::SectorEntityTypeComponent::Celestial());
            
            // 将地球放在轨道初始位置（角度0 = X轴正方向）
            auto& planetTransform = scene->GetRegistry().get<outer_wilds::TransformComponent>(planetEntity);
            planetTransform.position.x = planetOrbit.radius * cosf(planetOrbit.currentAngle);  // 100 * cos(0) = 100
            planetTransform.position.y = 0.0f;
            planetTransform.position.z = planetOrbit.radius * sinf(planetOrbit.currentAngle);  // 100 * sin(0) = 0
            
            std::stringstream orbitLog;
            orbitLog << "TEST: Planet orbit enabled at (" << planetTransform.position.x 
                     << ", " << planetTransform.position.y 
                     << ", " << planetTransform.position.z << ")";
            outer_wilds::DebugManager::GetInstance().Log("Main", orbitLog.str());
            
            outer_wilds::DebugManager::GetInstance().Log("Main", "Planet created successfully with orbit");
        } else {
            outer_wilds::DebugManager::GetInstance().Log("Main", "ERROR: Failed to create planet!");
        }


        // ========================================
        // 创建玩家（先创建，用于确定飞船位置）
        // ========================================
        outer_wilds::DebugManager::GetInstance().Log("Main", "=== Creating Player ===");
        
        // 获取地球当前位置
        auto& planetTransform = scene->GetRegistry().get<outer_wilds::TransformComponent>(planetEntity);
        
        auto playerEntity = scene->CreateEntity("player");
        auto& playerTransform = scene->GetRegistry().emplace<outer_wilds::TransformComponent>(playerEntity);
        
        // 玩家位置 = 地球位置 + 向上偏移（使用世界Y轴）
        playerTransform.position.x = planetTransform.position.x;
        playerTransform.position.y = planetTransform.position.y + PLAYER_START_HEIGHT;
        playerTransform.position.z = planetTransform.position.z;
        playerTransform.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        
        std::stringstream playerLog;
        playerLog << "Player spawn position: (" << playerTransform.position.x 
                  << ", " << playerTransform.position.y 
                  << ", " << playerTransform.position.z << ")";
        outer_wilds::DebugManager::GetInstance().Log("Main", playerLog.str());

        // ========================================
        // 继续配置玩家组件
        // ========================================
        
        // Add player component
        scene->GetRegistry().emplace<outer_wilds::PlayerComponent>(playerEntity);
        
        // === 添加重力影响组件（启用球面重力）===
        auto& playerGravity = scene->GetRegistry().emplace<outer_wilds::components::GravityAffectedComponent>(playerEntity);
        playerGravity.affectedByGravity = true;  // 启用球面重力
        playerGravity.gravityScale = 1.0f;
        
        // === 添加玩家对齐组件（启用自动对齐）===
        auto& playerAlignment = scene->GetRegistry().emplace<outer_wilds::components::PlayerAlignmentComponent>(playerEntity);
        playerAlignment.autoAlign = true;  // 启用自动对齐到星球表面
        playerAlignment.alignmentSpeed = 5.0f;
        playerAlignment.lockRoll = true;
        playerAlignment.preserveForward = true;
        playerAlignment.alignmentDeadZone = 0.5f;
        
        // === 添加玩家可视化模型（用于观察视角调试）===
        // player.obj尺寸：1m x 3.6m x 1m（Y轴从-1.8到1.8）
        // 需要缩小到适应胶囊体（高1.6m，半径0.4m）
        const float PLAYER_MODEL_SCALE = 0.25f; // 缩小到0.9m高
        entt::entity playerModelEntity = outer_wilds::SceneAssetLoader::LoadModelAsEntity(
            scene->GetRegistry(),
            scene,
            engine.GetRenderSystem()->GetBackend()->GetDevice(),
            "assets/BlendObj/player.obj",
            "",  // 不指定纹理，使用MTL文件中的Kd颜色
            playerTransform.position,
            DirectX::XMFLOAT3(PLAYER_MODEL_SCALE, PLAYER_MODEL_SCALE, PLAYER_MODEL_SCALE),
            nullptr  // 玩家模型不需要物理
        );
        
        if (playerModelEntity != entt::null) {
            outer_wilds::DebugManager::GetInstance().Log("Main", "Player visual model loaded");
            // 将玩家模型实体 ID 存储到 PlayerComponent 中，以便同步位置
            auto& playerComp = scene->GetRegistry().get<outer_wilds::PlayerComponent>(playerEntity);
            playerComp.visualModelEntity = playerModelEntity;
        } else {
            outer_wilds::DebugManager::GetInstance().Log("Main", "Failed to load player model");
        }
        
        // Create PhysX capsule character controller
        physx::PxCapsuleControllerDesc controllerDesc;
        controllerDesc.height = CONTROLLER_HEIGHT;
        controllerDesc.radius = CONTROLLER_RADIUS;
        controllerDesc.position = physx::PxExtendedVec3(playerTransform.position.x, playerTransform.position.y, playerTransform.position.z);
        
        std::stringstream ctrlLog;
        ctrlLog << "Controller setup: height=" << CONTROLLER_HEIGHT 
                << "m, radius=" << CONTROLLER_RADIUS 
                << "m, spawn at (" << playerTransform.position.x << ", " 
                << playerTransform.position.y << ", " << playerTransform.position.z << ")";
        outer_wilds::DebugManager::GetInstance().Log("Main", ctrlLog.str());
        
        controllerDesc.material = groundPhysicsMaterial;
        controllerDesc.stepOffset = 0.5f;      // 可以爬0.5米的台阶
        controllerDesc.contactOffset = CONTROLLER_CONTACT_OFFSET;
        controllerDesc.slopeLimit = cosf(DirectX::XM_PIDIV4); // 45度最大坡度
        controllerDesc.climbingMode = physx::PxCapsuleClimbingMode::eEASY;
        
        physx::PxController* controller = physxManager.GetControllerManager()->createController(controllerDesc);
        if (!controller) {
            outer_wilds::DebugManager::GetInstance().Log("Main", "Failed to create character controller!");
            return 0;
        }
        
        // 验证控制器实际位置
        physx::PxExtendedVec3 actualPos = controller->getPosition();
        std::stringstream verifyLog;
        verifyLog << "Controller created! Actual position: (" 
                  << (float)actualPos.x << ", " << (float)actualPos.y << ", " << (float)actualPos.z << ")";
        outer_wilds::DebugManager::GetInstance().Log("Main", verifyLog.str());
        
        // Add character controller component
        auto& characterController = scene->GetRegistry().emplace<outer_wilds::components::CharacterControllerComponent>(playerEntity);
        characterController.controller = controller;
        characterController.moveSpeed = 5.0f;
        characterController.jumpSpeed = 8.0f;
        characterController.gravity = -20.0f;
        
        // Add camera component (first-person view)
        auto& playerCamera = scene->GetRegistry().emplace<outer_wilds::components::CameraComponent>(playerEntity);
        playerCamera.isActive = true;
        playerCamera.fov = 75.0f;
        playerCamera.aspectRatio = 16.0f / 9.0f;
        playerCamera.nearPlane = 0.1f;
        playerCamera.farPlane = 1000.0f;
        // 相机位置/目标将由 PlayerSystem 在运行时根据 CharacterController 设置
        // 这里只设置初始值（会被第一帧覆盖）
        playerCamera.position = {0.0f, 0.0f, 0.0f};  // 临时值
        playerCamera.target = {0.0f, 0.0f, 1.0f};    // 临时值
        playerCamera.up = {0.0f, 1.0f, 0.0f};        // 默认 up
        playerCamera.yaw = 0.0f;
        playerCamera.pitch = 0.0f;
        playerCamera.relativeYaw = 0.0f;
        playerCamera.relativePitch = 0.0f;
        playerCamera.moveSpeed = 5.0f;
        playerCamera.lookSensitivity = 0.003f;
        
        // Add player input component
        auto& playerInput = scene->GetRegistry().emplace<outer_wilds::PlayerInputComponent>(playerEntity);
        playerInput.mouseLookEnabled = true;
        
        // 添加 Sector 归属组件（玩家属于地球 Sector）
        auto& playerInSector = scene->GetRegistry().emplace<outer_wilds::components::InSectorComponent>(playerEntity);
        playerInSector.currentSector = planetEntity;
        // 设置为未初始化，让 SectorSystem 在第一帧处理：
        // 1. 从世界坐标计算局部坐标
        // 2. 更新 CharacterController 到局部坐标
        playerInSector.isInitialized = false;
        
        // 添加玩家类型组件
        scene->GetRegistry().emplace<outer_wilds::components::SectorEntityTypeComponent>(
            playerEntity, outer_wilds::components::SectorEntityTypeComponent::Player());
        
        // 添加飞船交互组件
        scene->GetRegistry().emplace<outer_wilds::components::PlayerSpacecraftInteractionComponent>(playerEntity);
        
        // 自由相机将由CameraModeSystem动态创建（不在玩家实体上）
        outer_wilds::DebugManager::GetInstance().Log("Main", "Player created (Press Shift+ESC to toggle free camera)");
       
        // ========================================
        // 创建飞船（使用 FBX 模型）- 使用 Sector 坐标系统
        // ========================================
        {
            // 飞船位置：在玩家附近，行星表面上方
            // 注意：飞船绕X轴旋转-90度后，碰撞盒的Z轴变成了垂直方向
            // 碰撞盒是 PxBoxGeometry(3.0, 1.5, 4.0)，旋转后垂直方向半高是4m
            // 所以飞船中心需要在地面上方至少 4m + 安全余量
            const float SPACECRAFT_SURFACE_OFFSET = 20.0f;  // 【测试】放高一些看看是否与碰撞有关
            
            // 飞船位置计算：使用局部坐标（相对于 Sector/地球）
            // 与玩家相似的方向但在X方向偏移一些
            DirectX::XMFLOAT3 spacecraftDirection = { 0.3f, 1.0f, 0.0f };  // 略偏向X
            float dirLength = sqrtf(spacecraftDirection.x * spacecraftDirection.x + 
                                    spacecraftDirection.y * spacecraftDirection.y + 
                                    spacecraftDirection.z * spacecraftDirection.z);
            spacecraftDirection.x /= dirLength;
            spacecraftDirection.y /= dirLength;
            spacecraftDirection.z /= dirLength;
            
            float spacecraftHeight = PLANET_ACTUAL_RADIUS + SPACECRAFT_SURFACE_OFFSET;
            
            // 局部坐标（相对于 Sector 原点，即地球中心）
            DirectX::XMFLOAT3 spacecraftLocalPosition = {
                spacecraftDirection.x * spacecraftHeight,
                spacecraftDirection.y * spacecraftHeight,
                spacecraftDirection.z * spacecraftHeight
            };
            
            // 世界坐标（用于渲染）= Sector位置 + 局部坐标
            DirectX::XMFLOAT3 spacecraftWorldPosition = {
                planetTransform.position.x + spacecraftLocalPosition.x,
                planetTransform.position.y + spacecraftLocalPosition.y,
                planetTransform.position.z + spacecraftLocalPosition.z
            };
            
            std::cout << "[Main] Creating Spacecraft - Local: (" 
                     << spacecraftLocalPosition.x << ", " << spacecraftLocalPosition.y << ", " << spacecraftLocalPosition.z
                     << "), World: (" << spacecraftWorldPosition.x << ", " << spacecraftWorldPosition.y << ", " << spacecraftWorldPosition.z
                     << ")" << std::endl;
            
            // 飞船缩放（FBX模型较大，需要缩小）
            const float SPACECRAFT_SCALE = 0.02f;  // 缩小到2%
            
            // 使用 SceneAssetLoader 加载飞船模型（带多纹理）
            // 注意：使用世界坐标加载模型，之后会添加 InSectorComponent
            entt::entity spacecraftEntity = outer_wilds::SceneAssetLoader::LoadModelAsEntity(
                scene->GetRegistry(),
                scene,
                engine.GetRenderSystem()->GetBackend()->GetDevice(),
                "assets/models/spacecraft/base.fbx",
                "",  // 不指定纹理，FBX 内嵌或自动查找
                spacecraftWorldPosition,  // 使用世界坐标（渲染用）
                DirectX::XMFLOAT3(SPACECRAFT_SCALE, SPACECRAFT_SCALE, SPACECRAFT_SCALE),
                nullptr  // 物理稍后手动添加
            );
            
            if (spacecraftEntity != entt::null) {
                std::cout << "[Main] Spacecraft model loaded successfully!" << std::endl;
                
                // 修正飞船旋转：FBX模型可能是竖直的，需要绕X轴旋转-90度使其水平
                auto& spacecraftTransform = scene->GetRegistry().get<outer_wilds::TransformComponent>(spacecraftEntity);
                // 四元数表示绕X轴旋转-90度 (使Y轴朝上变为Z轴朝前)
                float angle = -DirectX::XM_PIDIV2;  // -90度
                spacecraftTransform.rotation = DirectX::XMFLOAT4(
                    sinf(angle * 0.5f), 0.0f, 0.0f, cosf(angle * 0.5f)
                );
                
                // 添加飞船组件 - 使用新的纯物理模拟字段
                auto& spacecraft = scene->GetRegistry().emplace<outer_wilds::components::SpacecraftComponent>(spacecraftEntity);
                spacecraft.mainThrust = 15000.0f;       // 主推力 15000N
                spacecraft.strafeThrust = 10000.0f;     // 侧向推力 10000N
                spacecraft.verticalThrust = 12000.0f;   // 垂直推力 12000N
                spacecraft.rollTorque = 8000.0f;        // 滚转力矩 8000N·m
                spacecraft.linearDamping = 0.3f;        // 线性阻尼
                spacecraft.angularDamping = 2.0f;       // 角阻尼
                spacecraft.mass = 500.0f;               // 质量 500kg
                spacecraft.interactionDistance = 10.0f;  // 10米交互距离
                spacecraft.exitOffset = { 5.0f, 0.0f, 0.0f };
                // 相机偏移：飞船已绕X轴旋转-90度，所以：
                // - 原来的Y轴（上）变成了Z轴（前）
                // - 原来的Z轴（前）变成了-Y轴（下）
                // 所以相机应该在飞船Z轴上方（前方）
                spacecraft.cameraOffset = { 0.0f, 1.0f, 3.0f };  // 座舱位置：稍微向上+向前
                
                // 添加重力影响组件
                auto& spacecraftGravity = scene->GetRegistry().emplace<outer_wilds::components::GravityAffectedComponent>(spacecraftEntity);
                spacecraftGravity.affectedByGravity = true;
                spacecraftGravity.gravityScale = 1.0f;
                
                // 注意：不再使用 GravityAlignmentComponent
                // 飞船通过纯物理模拟控制，玩家需要手动调整姿态
                
                // 创建 PhysX 刚体 - 使用局部坐标（相对于 Sector）
                auto* pxPhysics = outer_wilds::PhysXManager::GetInstance().GetPhysics();
                auto* pxScene = outer_wilds::PhysXManager::GetInstance().GetScene();
                
                const float SPACECRAFT_MASS = 300.0f;  // 飞船质量 (kg)
                const float SPACECRAFT_LINEAR_DRAG = 0.3f;  // 线性阻力
                const float SPACECRAFT_ANGULAR_DRAG = 1.5f;
                
                physx::PxMaterial* spacecraftMaterial = pxPhysics->createMaterial(0.5f, 0.5f, 0.1f);
                
                // 使用局部坐标创建 PhysX 碰撞体，包含正确的旋转
                // 先获取 Transform 的旋转
                const auto& spacecraftRot = spacecraftTransform.rotation;
                physx::PxTransform spacecraftPose(
                    physx::PxVec3(spacecraftLocalPosition.x, spacecraftLocalPosition.y, spacecraftLocalPosition.z),
                    physx::PxQuat(spacecraftRot.x, spacecraftRot.y, spacecraftRot.z, spacecraftRot.w)
                );
                
                physx::PxRigidDynamic* spacecraftActor = pxPhysics->createRigidDynamic(spacecraftPose);
                
                // 【测试】使用 createExclusiveShape 创建形状（PhysX 5 推荐方式）
                physx::PxShape* spacecraftShape = physx::PxRigidActorExt::createExclusiveShape(
                    *spacecraftActor,
                    physx::PxSphereGeometry(3.0f),
                    *spacecraftMaterial
                );
                spacecraftShape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE, true);
                spacecraftShape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, true);
                
                // 【重要】设置接触偏移，帮助 PhysX 提前检测碰撞
                spacecraftShape->setContactOffset(0.1f);  // 接触偏移 0.1 米
                spacecraftShape->setRestOffset(0.02f);    // 休眠偏移 0.02 米
                
                // 【测试】暂时禁用飞船的碰撞，只测试简单球体
                spacecraftShape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
                
                // 添加到场景
                pxScene->addActor(*spacecraftActor);
                
                // ========================================
                // 【测试】创建一个最简单的测试球体（行星是 Box）
                // ========================================
                std::cout << "[TEST] Creating simple test SPHERE (planet is BOX)..." << std::endl;
                physx::PxMaterial* testMaterial = pxPhysics->createMaterial(0.5f, 0.5f, 0.3f);
                physx::PxTransform testPose(physx::PxVec3(0.0f, 80.0f, 10.0f));  // 行星上方80米
                physx::PxRigidDynamic* testSphere = pxPhysics->createRigidDynamic(testPose);
                
                // 使用球体碰撞体
                physx::PxShape* testShape = physx::PxRigidActorExt::createExclusiveShape(
                    *testSphere,
                    physx::PxSphereGeometry(1.0f),  // 1米半径的球
                    *testMaterial
                );
                testShape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE, true);
                testShape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, true);
                
                // 设置质量
                physx::PxRigidBodyExt::updateMassAndInertia(*testSphere, 10.0f);  // 密度=10, 质量约42kg
                testSphere->setLinearDamping(0.1f);
                testSphere->setAngularDamping(0.1f);
                
                // 添加到场景
                pxScene->addActor(*testSphere);
                testSphere->wakeUp();
                
                std::cout << "[TEST] Test sphere created at (0, 80, 10) with radius=1m, mass=" 
                          << testSphere->getMass() << "kg" << std::endl;
                std::cout << "[TEST] Planet collision radius = 64m, test sphere should collide at dist=65m" << std::endl;
                
                // 【测试】增加求解器迭代次数，提高碰撞稳定性
                spacecraftActor->setSolverIterationCounts(8, 4);  // 8 位置迭代, 4 速度迭代
                
                // 【测试】暂时禁用 CCD 看是否 CCD 导致崩溃
                // spacecraftActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, true);
                
                // 使用密度1来计算惯性张量，然后手动设置质量
                physx::PxRigidBodyExt::updateMassAndInertia(*spacecraftActor, 1.0f);  // 密度=1
                spacecraftActor->setMass(SPACECRAFT_MASS);  // 手动设置质量
                
                spacecraftActor->setLinearDamping(SPACECRAFT_LINEAR_DRAG);
                spacecraftActor->setAngularDamping(SPACECRAFT_ANGULAR_DRAG);
                
                // 设置最大速度限制，防止速度过快导致穿透
                spacecraftActor->setMaxLinearVelocity(100.0f);  // 最大100 m/s
                spacecraftActor->setMaxAngularVelocity(10.0f);  // 最大10 rad/s
                
                spacecraftActor->wakeUp();  // 确保飞船处于活跃状态
                
                // 添加刚体组件
                auto& spacecraftRigidBody = scene->GetRegistry().emplace<outer_wilds::RigidBodyComponent>(spacecraftEntity);
                spacecraftRigidBody.physxActor = spacecraftActor;
                spacecraftRigidBody.mass = SPACECRAFT_MASS;
                spacecraftRigidBody.drag = SPACECRAFT_LINEAR_DRAG;
                spacecraftRigidBody.angularDrag = SPACECRAFT_ANGULAR_DRAG;
                spacecraftRigidBody.useGravity = true;  // 【测试】启用重力
                spacecraftRigidBody.isKinematic = false;  // 【测试】Dynamic模式
                
                // 添加 Sector 组件，使飞船成为地球 Sector 的一部分
                auto& spacecraftInSector = scene->GetRegistry().emplace<outer_wilds::components::InSectorComponent>(spacecraftEntity);
                spacecraftInSector.currentSector = planetEntity;
                spacecraftInSector.localPosition = spacecraftLocalPosition;
                // 初始旋转：让飞船底部朝向地球中心（Y轴向上对齐重力方向）
                spacecraftInSector.localRotation = scene->GetRegistry().get<outer_wilds::TransformComponent>(spacecraftEntity).rotation;
                spacecraftInSector.isInitialized = true;
                
                // 添加 Sector 实体类型组件（飞船类型）
                scene->GetRegistry().emplace<outer_wilds::components::SectorEntityTypeComponent>(
                    spacecraftEntity, 
                    outer_wilds::components::SectorEntityTypeComponent::Spacecraft()
                );
                
                std::cout << "[Main] Spacecraft created in Sector 'Earth'! Press F near it to enter." << std::endl;
                std::cout << "[Main] Spacecraft Local Position: (" << spacecraftLocalPosition.x << ", " 
                         << spacecraftLocalPosition.y << ", " << spacecraftLocalPosition.z << ")" << std::endl;
                std::cout << "[Main] Spacecraft controls: WASD=move, Shift/Ctrl=up/down, Q/E=roll" << std::endl;
            } else {
                std::cout << "[Main] ERROR: Failed to load spacecraft model!" << std::endl;
            }
        }
        
        // ========================================
        // 创建月球(地球卫星)
        // ========================================
        const float MOON_ORBIT_RADIUS = 90.0f;   // 轨道半径 8m
        const float MOON_RADIUS = 1.5f;         // 月球半径 1.5m
        const float MOON_SCALE = MOON_RADIUS / PLANET_MODEL_RADIUS; // 基于planet1.obj缩放
        
        // 月球初始位置:在地球右侧
        DirectX::XMFLOAT3 moonPosition = {MOON_ORBIT_RADIUS, 0.0f, 0.0f};
        
        std::stringstream moonLog;
        moonLog << "Creating Moon: orbit_radius=" << MOON_ORBIT_RADIUS 
                << "m, moon_radius=" << MOON_RADIUS << "m, scale=" << MOON_SCALE;
        outer_wilds::DebugManager::GetInstance().Log("Main", moonLog.str());
        
        // 创建月球物理配置 - 【测试】暂时禁用碰撞
        outer_wilds::PhysicsOptions moonPhysics;
        moonPhysics.addCollider = false;  // 禁用碰撞测试
        moonPhysics.addRigidBody = false; // 禁用刚体测试
        moonPhysics.shape = outer_wilds::PhysicsOptions::ColliderShape::Sphere;
        moonPhysics.sphereRadius = PLANET_MODEL_RADIUS;
        moonPhysics.mass = 0.0f;
        moonPhysics.useGravity = false;
        moonPhysics.isKinematic = true;
        moonPhysics.staticFriction = 0.8f;
        moonPhysics.dynamicFriction = 0.6f;
        moonPhysics.restitution = 0.2f;
        
        // 使用相同的星球模型和纹理
        entt::entity moonEntity = outer_wilds::SceneAssetLoader::LoadModelAsEntity(
            scene->GetRegistry(),
            scene,
            engine.GetRenderSystem()->GetBackend()->GetDevice(),
            "assets/BlendObj/planet1.obj",
            "assets/Texture/plastered_stone_wall_diff_2k.jpg",
            moonPosition,
            DirectX::XMFLOAT3(MOON_SCALE, MOON_SCALE, MOON_SCALE),
            &moonPhysics
        );
        
        if (moonEntity != entt::null) {
            // 添加月球的重力源(较弱)
            auto& moonGravitySource = scene->GetRegistry().emplace<outer_wilds::components::GravitySourceComponent>(moonEntity);
            moonGravitySource.radius = MOON_RADIUS;
            moonGravitySource.surfaceGravity = 1.62f;  // 月球真实重力约为地球的1/6
            moonGravitySource.atmosphereHeight = 1.5f;  // 影响范围3m
            moonGravitySource.useRealisticGravity = false;
            moonGravitySource.isActive = true;
            
            // 添加轨道组件 - 让月球围绕地球公转
            auto& moonOrbit = scene->GetRegistry().emplace<outer_wilds::components::OrbitComponent>(moonEntity);
            moonOrbit.centerEntity = planetEntity;  // 围绕地球
            moonOrbit.radius = MOON_ORBIT_RADIUS;
            moonOrbit.period = 30.0f;  // 30秒公转一圈（方便观察）
            moonOrbit.currentAngle = 0.0f;  // 从正X轴开始
            moonOrbit.isActive = true;
            
            
        } else {
            outer_wilds::DebugManager::GetInstance().Log("Main", "Failed to load moon model");
        }
        
        // ========================================
        // 输出完整的启动日志
        // ========================================
        std::cout << "\n=== STARTUP LOGS ===" << std::endl;
        outer_wilds::DebugManager::GetInstance().ForcePrint();
        std::cout << "===================\n" << std::endl;
        
        // ========================================
        // 启动流程：欢迎界面 -> 等待按键 -> 进入游戏
        // ========================================
        
        // 1. 显示欢迎界面（等待按键）
        if (auto uiSystem = engine.GetUISystem()) {
            uiSystem->ShowWelcomeScreenWithKeyWait("C:\\Users\\kkakk\\homework\\OuterWilds\\assets\\ui\\kkstudio1.jpg");
            outer_wilds::DebugManager::GetInstance().Log("Main", "Welcome screen displayed, waiting for key press");
        }
        
        // 2. 播放欢迎音乐（02 - Outer Wilds.mp3）
        if (auto audioSystem = engine.GetAudioSystem()) {
            if (audioSystem->PlaySingleTrack("C:\\Users\\kkakk\\homework\\OuterWilds\\assets\\Outer Wilds (Original Soundtrack)\\02 - Outer Wilds.mp3")) {
                outer_wilds::DebugManager::GetInstance().Log("Main", "Welcome music started");
            }
        }
        
        // 3. 主循环 - 等待按键
        bool welcomeFinished = false;
        bool shouldExit = false;
        while (engine.GetUISystem() && engine.GetUISystem()->IsWelcomeScreenVisible() && !shouldExit) {
            // 处理消息
            MSG msg = {};
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
                if (msg.message == WM_QUIT) {
                    shouldExit = true;
                    engine.Stop();
                    break;
                }
            }
            
            // 更新引擎（让UI和音频系统工作）
            outer_wilds::TimeManager::GetInstance().Update();
            float deltaTime = outer_wilds::TimeManager::GetInstance().GetDeltaTime();
            
            auto& registry = scene->GetRegistry();
            // 更新UI和音频系统
            if (auto uiSys = engine.GetUISystem()) {
                uiSys->Update(deltaTime, registry);
            }
            if (auto audioSys = engine.GetAudioSystem()) {
                audioSys->Update(deltaTime, registry);
            }
            
            // 渲染欢迎界面
            if (auto renderSystem = engine.GetRenderSystem()) {
                auto renderBackend = renderSystem->GetBackend();
                if (renderBackend) {
                    auto context = static_cast<ID3D11DeviceContext*>(renderBackend->GetContext());
                    auto renderTargetView = static_cast<ID3D11RenderTargetView*>(renderBackend->GetRenderTargetView());
                    auto depthStencilView = static_cast<ID3D11DepthStencilView*>(renderBackend->GetDepthStencilView());
                    
                    if (context && renderTargetView && depthStencilView) {
                        context->OMSetRenderTargets(1, &renderTargetView, depthStencilView);
                        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
                        context->ClearRenderTargetView(renderTargetView, clearColor);
                        context->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
                    }
                }
                
                if (auto uiSystem = engine.GetUISystem()) {
                    uiSystem->Render();
                }
                
                if (renderBackend) {
                    renderBackend->EndFrame();
                    renderBackend->Present();
                }
            }
            
            // 检查是否按下按键
            if (engine.GetUISystem() && engine.GetUISystem()->WasKeyPressed()) {
                welcomeFinished = true;
                outer_wilds::DebugManager::GetInstance().Log("Main", "Key pressed, transitioning to game");
                break;
            }
        }
        
        // 如果用户选择退出，直接跳过后续流程
        if (shouldExit) {
            engine.Shutdown();
            return 0;
        }
        
        // 4. 切换到游戏音乐
        if (welcomeFinished) {
            if (auto audioSystem = engine.GetAudioSystem()) {
                std::string musicFolder = "C:\\Users\\kkakk\\homework\\OuterWilds\\assets\\Outer Wilds (Original Soundtrack)";
                if (audioSystem->LoadPlaylistFromDirectory(musicFolder)) {
                    audioSystem->SetLoopPlaylist(true);
                    audioSystem->Play();
                    outer_wilds::DebugManager::GetInstance().Log("Main", "Game music started");
                }
            }
        }
        
        // 5. 进入正常游戏循环
        outer_wilds::DebugManager::GetInstance().Log("Main", "Entering main game loop");
        engine.Run();
    }
    
    engine.Shutdown();
    
    return 0;
    
}
