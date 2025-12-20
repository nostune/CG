#include "core/Engine.h"
#include "core/DebugManager.h"
#include "scene/Scene.h"
#include "scene/SceneAssetLoader.h"
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
#include "gameplay/components/StandingOnComponent.h"
#include "gameplay/OrbitSystem.h"
#include "physics/components/GravitySourceComponent.h"
#include "physics/components/GravityAffectedComponent.h"
#include "physics/components/RigidBodyComponent.h"
#include "graphics/resources/Material.h"
#include "graphics/resources/Shader.h"
#include "graphics/resources/TextureLoader.h"
#include "graphics/RenderSystem.h"
#include "physics/PhysXManager.h"
#include <PxPhysicsAPI.h>
#include <Windows.h>
#include <iostream>
#include <sstream>

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
    switch (uMsg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_CLOSE:
            outer_wilds::Engine::GetInstance().Stop();
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
        planetPhysics.addRigidBody = true;
        planetPhysics.shape = outer_wilds::PhysicsOptions::ColliderShape::Sphere;
        planetPhysics.sphereRadius = PLANET_MODEL_RADIUS;  // 6.4米,会自动乘scale变成64米
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
            
            // 【测试】添加轨道组件 - 让地球围绕原点公转以测试玩家跟随
            auto& planetOrbit = scene->GetRegistry().emplace<outer_wilds::components::OrbitComponent>(planetEntity);
            planetOrbit.centerEntity = entt::null;  // 围绕世界原点
            planetOrbit.radius = 100.0f;  // 100米轨道半径
            planetOrbit.period = 60.0f;   // 60秒一圈（慢速，方便观察）
            planetOrbit.currentAngle = 0.0f;  // 从X轴正方向开始
            planetOrbit.isActive = true;
            
            // 立即将地球移动到轨道起始位置
            auto& planetTransform = scene->GetRegistry().get<outer_wilds::TransformComponent>(planetEntity);
            planetTransform.position.x = planetOrbit.radius * cosf(planetOrbit.currentAngle);
            planetTransform.position.y = 0.0f;
            planetTransform.position.z = planetOrbit.radius * sinf(planetOrbit.currentAngle);
            
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
        // 创建参考网格平面
        // ========================================
        auto gridEntity = scene->CreateEntity("grid_reference");
        auto& gridTransform = scene->GetRegistry().emplace<outer_wilds::TransformComponent>(gridEntity);
        gridTransform.position = {0.0f, 0.0f, 0.0f};
        gridTransform.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        gridTransform.scale = {1.0f, 1.0f, 1.0f};
        
        auto& gridMeshComponent = scene->GetRegistry().emplace<outer_wilds::components::MeshComponent>(gridEntity);
        gridMeshComponent.mesh = groundMesh;
        gridMeshComponent.material = groundRenderMaterial;
        gridMeshComponent.isVisible = true;
        gridMeshComponent.castsShadows = false;
        
        auto& gridRenderPriority = scene->GetRegistry().emplace<outer_wilds::components::RenderPriorityComponent>(gridEntity);
        gridRenderPriority.renderPass = 0;
        gridRenderPriority.sortKey = 50;
        gridRenderPriority.lodLevel = 0;
        
        outer_wilds::DebugManager::GetInstance().Log("Main", "Reference grid created");

        // ========================================
        // 创建玩家
        // ========================================
        outer_wilds::DebugManager::GetInstance().Log("Main", "=== Creating Player ===");
        
        auto playerEntity = scene->CreateEntity("player");
        auto& playerTransform = scene->GetRegistry().emplace<outer_wilds::TransformComponent>(playerEntity);
        
        // 获取地球当前位置（已经在轨道起始点）
        auto& planetTransform = scene->GetRegistry().get<outer_wilds::TransformComponent>(planetEntity);
        
        // 玩家位置 = 地球位置 + 向上偏移（使用世界Y轴）
        playerTransform.position.x = planetTransform.position.x;
        playerTransform.position.y = planetTransform.position.y + PLAYER_START_HEIGHT;
        playerTransform.position.z = planetTransform.position.z;
        playerTransform.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        
        std::stringstream playerLog;
        playerLog << "Player spawn position: (" << playerTransform.position.x 
                  << ", " << playerTransform.position.y 
                  << ", " << playerTransform.position.z << ")\n"
                  << "  - Planet position: (" << planetTransform.position.x 
                  << ", " << planetTransform.position.y 
                  << ", " << planetTransform.position.z << ")\n"
                  << "  - Planet radius: " << PLANET_ACTUAL_RADIUS << "m\n"
                  << "  - Height above planet center: " << PLAYER_START_HEIGHT << "m";
        outer_wilds::DebugManager::GetInstance().Log("Main", playerLog.str());
        
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
        playerCamera.position = playerTransform.position;
        // 向下看行星中心 (从 y=65.8 看向 y=0)
        playerCamera.target = {0.0f, 0.0f, 0.0f};
        playerCamera.up = {0.0f, 0.0f, 1.0f};  // 使用Z轴作为up向量(适应向下看)
        playerCamera.yaw = 0.0f;
        playerCamera.pitch = -90.0f;  // 向下90度
        playerCamera.moveSpeed = 5.0f;
        playerCamera.lookSensitivity = 0.003f;
        
        // Add player input component
        auto& playerInput = scene->GetRegistry().emplace<outer_wilds::PlayerInputComponent>(playerEntity);
        playerInput.mouseLookEnabled = true;
        
        // Add standing-on component (让玩家跟随移动的天体)
        auto& standingOn = scene->GetRegistry().emplace<outer_wilds::components::StandingOnComponent>(playerEntity);
        standingOn.followEnabled = true;
        standingOn.detectionThreshold = 5.0f;
        
        // 自由相机将由CameraModeSystem动态创建（不在玩家实体上）
        outer_wilds::DebugManager::GetInstance().Log("Main", "Player created (Press Shift+ESC to toggle free camera)");
        
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
        
        // 创建月球物理配置
        outer_wilds::PhysicsOptions moonPhysics;
        moonPhysics.addCollider = true;
        moonPhysics.addRigidBody = true;
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
            
            outer_wilds::DebugManager::GetInstance().Log("Main", "Moon created successfully with orbit");
        } else {
            outer_wilds::DebugManager::GetInstance().Log("Main", "Failed to load moon model");
        }
        
        // ========================================
        // 输出完整的启动日志
        // ========================================
        std::cout << "\n=== STARTUP LOGS ===" << std::endl;
        outer_wilds::DebugManager::GetInstance().ForcePrint();
        std::cout << "===================\n" << std::endl;
        
        engine.Run();
    }
    
    engine.Shutdown();
    
    return 0;
    
}
