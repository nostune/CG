#include "core/Engine.h"
#include "core/DebugManager.h"
#include "scene/Scene.h"
#include "scene/SceneAssetLoader.h"
#include "scene/components/TransformComponent.h"
#include "graphics/components/RenderableComponent.h"
#include "graphics/components/RenderPriorityComponent.h"
#include "graphics/components/CameraComponent.h"
#include "graphics/components/MeshComponent.h"
#include "gameplay/components/PlayerInputComponent.h"
#include "gameplay/components/CharacterControllerComponent.h"
#include "graphics/resources/Material.h"
#include "graphics/resources/Shader.h"
#include "graphics/RenderSystem.h"
#include "physics/PhysXManager.h"
#include <PxPhysicsAPI.h>
#include <Windows.h>
#include <iostream>

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
        // Get the default scene
        auto scene = engine.GetSceneManager()->GetActiveScene();

        // Create a PhysX ground plane with friction
        auto& physxManager = outer_wilds::PhysXManager::GetInstance();
        physx::PxTransform planeTransform(physx::PxVec3(0.0f, 0.0f, 0.0f), physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0.0f, 0.0f, 1.0f)));
        physx::PxRigidStatic* groundPlane = physxManager.GetPhysics()->createRigidStatic(planeTransform);
        
        // Create ground physics material with friction
        physx::PxMaterial* groundPhysicsMaterial = physxManager.GetPhysics()->createMaterial(0.8f, 0.6f, 0.1f); // static, dynamic, restitution
        physx::PxShape* planeShape = physxManager.GetPhysics()->createShape(physx::PxPlaneGeometry(), *groundPhysicsMaterial);
        groundPlane->attachShape(*planeShape);
        planeShape->release();
        physxManager.GetScene()->addActor(*groundPlane);
        
        // Create ECS entity for rendering the ground plane as a simple quad
        auto ground = scene->CreateEntity("ground");
        auto& groundTransform = scene->GetRegistry().emplace<outer_wilds::TransformComponent>(ground);
        groundTransform.position = {0.0f, 0.0f, 0.0f}; // Ground at y=0
        groundTransform.scale = {1.0f, 1.0f, 1.0f};

        // Create a simple quad mesh for the ground plane
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
        
        // Create and load the grid shader
        auto groundShader = std::make_shared<outer_wilds::resources::Shader>();
        groundShader->LoadFromFile(engine.GetRenderSystem()->GetBackend()->GetDevice(), "grid.vs", "grid.ps");
        
        auto& groundRenderable = scene->GetRegistry().emplace<outer_wilds::components::RenderableComponent>(ground);
        groundRenderable.mesh = groundMesh;
        groundRenderable.material = groundRenderMaterial;
        groundRenderable.shader = groundShader;
        
        // 添加 RenderPriorityComponent（标签管理）
        auto& groundPriority = scene->GetRegistry().emplace<outer_wilds::components::RenderPriorityComponent>(ground);
        groundPriority.sortKey = 1000;  // 地面的排序键
        groundPriority.renderPass = 0;  // Opaque（不透明）
        groundPriority.lodLevel = 0;    // 最高细节
        groundPriority.visible = true;  // 默认可见
        groundPriority.castShadow = false;  // 地面不投射阴影
        groundPriority.receiveShadow = true;  // 地面接收阴影

        // ========================================
        // Load Blender Sphere Model with Texture
        // ========================================
        outer_wilds::DebugManager::GetInstance().Log("Main", "Loading sphere model from assets...");
        
        entt::entity sphereEntity = outer_wilds::SceneAssetLoader::LoadModelAsEntity(
            scene->GetRegistry(),
            scene,
            engine.GetRenderSystem()->GetBackend()->GetDevice(),
            "assets/BlendObj/planet1.obj",
            "assets/Texture/plastered_stone_wall_diff_2k.jpg",
            DirectX::XMFLOAT3(3.0f, 2.0f, 0.0f),  // Position: 3m to the right, 2m up
            DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f)   // Scale: 1x
        );

        if (sphereEntity != entt::null) {
            outer_wilds::DebugManager::GetInstance().Log("Main", "Sphere entity created successfully!");
            
            // Optional: Add physics to the sphere (static or dynamic)
            // For now, it's just a visual object following ECS pattern
        } else {
            outer_wilds::DebugManager::GetInstance().Log("Main", "Failed to load sphere entity!");
        }

        // ========================================
        // For Large Scenes: Best Practices
        // ========================================
        // 1. Use SceneAssetLoader::LoadSceneFromFile() to load entire scene from JSON/XML
        //    Example scene file structure:
        //    {
        //      "objects": [
        //        {"model": "planet1.obj", "texture": "stone.jpg", "position": [0,0,0]},
        //        {"model": "planet2.obj", "texture": "stone.jpg", "position": [10,0,0]},
        //        ...hundreds of objects...
        //      ]
        //    }
        //
        // 2. Share mesh/material resources for repeated objects (instancing):
        //    auto sharedMesh = SceneAssetLoader::LoadMeshResource("planet1.obj");
        //    auto sharedMat = SceneAssetLoader::CreateMaterialResource(device, "stone.jpg");
        //    for (int i = 0; i < 100; i++) {
        //        auto entity = registry.create();
        //        registry.emplace<TransformComponent>(entity, position, rotation, scale);
        //        registry.emplace<MeshComponent>(entity, sharedMesh, sharedMat);  // Reuse!
        //    }
        //
        // 3. Use spatial partitioning (Octree/BVH) for culling:
        //    - Only render entities visible to camera
        //    - Implement frustum culling in RenderSystem
        //
        // 4. Level of Detail (LOD):
        //    - Store multiple mesh versions with different detail
        //    - Switch based on distance from camera
        //    - Use RenderPriorityComponent.lodLevel
        //
        // 5. Async loading:
        //    - Load heavy assets (models/textures) in background thread
        //    - Create entities in main thread once loaded
        //
        // 6. Each object MUST have these components:
        //    - TransformComponent: Position, rotation, scale
        //    - MeshComponent: Shared mesh + material resources
        //    - RenderableComponent: Visibility, shadows
        //    - RenderPriorityComponent: Sort key, render pass, LOD
        //    - Optional: PhysicsComponent for collision
        // ========================================

        // Create a player entity with character controller
        auto playerEntity = scene->CreateEntity("player");
        auto& playerTransform = scene->GetRegistry().emplace<outer_wilds::TransformComponent>(playerEntity);
        playerTransform.position = {0.0f, 2.0f, -5.0f}; // Start above ground
        playerTransform.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        
        // Create PhysX capsule character controller
        physx::PxCapsuleControllerDesc controllerDesc;
        controllerDesc.height = 1.6f;          // 身高1.6米
        controllerDesc.radius = 0.4f;          // 半径0.4米
        controllerDesc.position = physx::PxExtendedVec3(playerTransform.position.x, playerTransform.position.y, playerTransform.position.z);
        controllerDesc.material = groundPhysicsMaterial;  // 使用相同的材质以获得摩擦力
        controllerDesc.stepOffset = 0.5f;      // 可以爬0.5米的台阶
        controllerDesc.contactOffset = 0.1f;
        controllerDesc.slopeLimit = cosf(DirectX::XM_PIDIV4); // 45度最大坡度
        controllerDesc.climbingMode = physx::PxCapsuleClimbingMode::eEASY;
        
        physx::PxController* controller = physxManager.GetControllerManager()->createController(controllerDesc);
        if (!controller) {
            outer_wilds::DebugManager::GetInstance().Log("Main", "Failed to create character controller!");
            return 0;
        }
        
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
        playerCamera.target = {0.0f, 2.0f, 0.0f}; // Look forward
        playerCamera.up = {0.0f, 1.0f, 0.0f};
        playerCamera.yaw = 0.0f;
        playerCamera.pitch = 0.0f;
        playerCamera.moveSpeed = 5.0f;
        playerCamera.lookSensitivity = 0.003f;
        
        // Add player input component
        auto& playerInput = scene->GetRegistry().emplace<outer_wilds::PlayerInputComponent>(playerEntity);
        playerInput.mouseLookEnabled = true; // 默认启用鼠标视角
        
        engine.Run();
    }
    
    engine.Shutdown();
    
    return 0;
}
