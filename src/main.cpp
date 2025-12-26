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
            
            // 【测试】添加轨道组件 - 启用公转
            auto& planetOrbit = scene->GetRegistry().emplace<outer_wilds::components::OrbitComponent>(planetEntity);
            planetOrbit.centerEntity = entt::null;  // 围绕世界原点
            planetOrbit.radius = 100.0f;  // 100米轨道半径
            planetOrbit.period = 60.0f;   // 60秒一圈（慢速，方便观察）
            planetOrbit.currentAngle = 0.0f;  // 从X轴正方向开始
            planetOrbit.isActive = true;  // 启用公转
            
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
        
        // 添加飞船交互组件
        scene->GetRegistry().emplace<outer_wilds::components::PlayerSpacecraftInteractionComponent>(playerEntity);
        
        // 自由相机将由CameraModeSystem动态创建（不在玩家实体上）
        outer_wilds::DebugManager::GetInstance().Log("Main", "Player created (Press Shift+ESC to toggle free camera)");
        
        // ========================================
        // 创建飞船（使用 FBX 模型）
        // ========================================
        {
            // 飞船位置：在玩家附近，行星表面上方
            // 使用球坐标计算，放在地球表面上方，与玩家相邻
            const float SPACECRAFT_SURFACE_OFFSET = 1.5f;  // 比地表高1.5米（贴近地面）
            
            // 飞船位置计算：从地球中心沿Y+方向（和玩家相同）但在X方向偏移一些
            // 使用向量计算确保在球面上
            DirectX::XMFLOAT3 spacecraftDirection = { 0.3f, 1.0f, 0.0f };  // 略偏向X
            float dirLength = sqrtf(spacecraftDirection.x * spacecraftDirection.x + 
                                    spacecraftDirection.y * spacecraftDirection.y + 
                                    spacecraftDirection.z * spacecraftDirection.z);
            spacecraftDirection.x /= dirLength;
            spacecraftDirection.y /= dirLength;
            spacecraftDirection.z /= dirLength;
            
            float spacecraftHeight = PLANET_ACTUAL_RADIUS + SPACECRAFT_SURFACE_OFFSET;
            DirectX::XMFLOAT3 spacecraftPosition = {
                planetTransform.position.x + spacecraftDirection.x * spacecraftHeight,
                planetTransform.position.y + spacecraftDirection.y * spacecraftHeight,
                planetTransform.position.z + spacecraftDirection.z * spacecraftHeight
            };
            
            std::cout << "[Main] Creating Spacecraft at (" 
                     << spacecraftPosition.x << ", " << spacecraftPosition.y << ", " << spacecraftPosition.z
                     << ")" << std::endl;
            
            // 飞船缩放（FBX模型较大，需要缩小）
            const float SPACECRAFT_SCALE = 0.02f;  // 缩小到2%
            
            // 使用 SceneAssetLoader 加载飞船模型（带多纹理）
            entt::entity spacecraftEntity = outer_wilds::SceneAssetLoader::LoadModelAsEntity(
                scene->GetRegistry(),
                scene,
                engine.GetRenderSystem()->GetBackend()->GetDevice(),
                "assets/models/spacecraft/base.fbx",
                "",  // 不指定纹理，FBX 内嵌或自动查找
                spacecraftPosition,
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
                
                // 添加飞船组件
                auto& spacecraft = scene->GetRegistry().emplace<outer_wilds::components::SpacecraftComponent>(spacecraftEntity);
                spacecraft.thrustPower = 10.0f;  // 水平推力（降低）
                spacecraft.verticalThrustPower = 15.0f;  // 垂直推力（刚好能克服重力）
                spacecraft.angularAcceleration = 2.0f;  // 降低角加速度
                spacecraft.maxAngularSpeed = 1.5f;  // 降低最大角速度
                spacecraft.angularDamping = 2.5f;
                spacecraft.mouseSensitivity = 0.15f;
                spacecraft.interactionDistance = 10.0f;  // 10米交互距离
                spacecraft.exitOffset = { 3.0f, 0.0f, 0.0f };
                spacecraft.cameraOffset = { 0.0f, 2.0f, 0.0f };  // 座舱位置
                spacecraft.landingAssistAltitude = 15.0f;  // 15米高度阈值（适合小星球）
                
                // 添加重力影响组件
                auto& spacecraftGravity = scene->GetRegistry().emplace<outer_wilds::components::GravityAffectedComponent>(spacecraftEntity);
                spacecraftGravity.affectedByGravity = true;
                spacecraftGravity.gravityScale = 1.0f;
                
                // 添加重力对齐组件（飞船使用 ASSIST_ALIGN 模式）
                auto& spacecraftAlignment = scene->GetRegistry().emplace<outer_wilds::components::GravityAlignmentComponent>(spacecraftEntity);
                spacecraftAlignment.mode = outer_wilds::components::GravityAlignmentComponent::AlignmentMode::ASSIST_ALIGN;
                spacecraftAlignment.alignmentSpeed = 2.0f;
                
                // 创建 PhysX 刚体
                auto* pxPhysics = outer_wilds::PhysXManager::GetInstance().GetPhysics();
                auto* pxScene = outer_wilds::PhysXManager::GetInstance().GetScene();
                
                const float SPACECRAFT_MASS = 300.0f;  // 增加质量
                const float SPACECRAFT_LINEAR_DRAG = 0.3f;  // 增加阻力
                const float SPACECRAFT_ANGULAR_DRAG = 1.5f;
                
                physx::PxMaterial* spacecraftMaterial = pxPhysics->createMaterial(0.5f, 0.5f, 0.1f);
                
                // 使用盒子碰撞器，尺寸根据飞船模型调整
                physx::PxShape* spacecraftShape = pxPhysics->createShape(
                    physx::PxBoxGeometry(3.0f, 1.5f, 4.0f),  // 6m x 3m x 8m 的盒子
                    *spacecraftMaterial
                );
                
                physx::PxTransform spacecraftPose(
                    physx::PxVec3(spacecraftPosition.x, spacecraftPosition.y, spacecraftPosition.z)
                );
                
                physx::PxRigidDynamic* spacecraftActor = pxPhysics->createRigidDynamic(spacecraftPose);
                spacecraftActor->attachShape(*spacecraftShape);
                physx::PxRigidBodyExt::updateMassAndInertia(*spacecraftActor, SPACECRAFT_MASS);
                spacecraftActor->setLinearDamping(SPACECRAFT_LINEAR_DRAG);
                spacecraftActor->setAngularDamping(SPACECRAFT_ANGULAR_DRAG);
                
                // 设置为 Kinematic 模式（与 staticAttach = true 对应）
                // 这样在玩家输入起飞命令前，飞船会保持静止附着在地面
                spacecraftActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
                
                pxScene->addActor(*spacecraftActor);
                
                // 添加刚体组件
                auto& spacecraftRigidBody = scene->GetRegistry().emplace<outer_wilds::RigidBodyComponent>(spacecraftEntity);
                spacecraftRigidBody.physxActor = spacecraftActor;
                spacecraftRigidBody.mass = SPACECRAFT_MASS;
                spacecraftRigidBody.drag = SPACECRAFT_LINEAR_DRAG;
                spacecraftRigidBody.angularDrag = SPACECRAFT_ANGULAR_DRAG;
                spacecraftRigidBody.useGravity = false;
                spacecraftRigidBody.isKinematic = true;  // 与 PhysX actor 的 kinematic 标志同步
                
                std::cout << "[Main] Spacecraft created! Press E near it to enter." << std::endl;
                std::cout << "[Main] Spacecraft controls: WASD=move, Mouse=look, Shift/Ctrl=up/down, L=landing assist" << std::endl;
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
