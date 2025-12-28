/**
 * main.cpp
 * 
 * Outer Wilds ECS 主程序入口
 * 
 * 【物理架构】
 * 更新顺序: Input → Systems → PhysX simulate/fetchResults → Render
 * 任何修改 PhysX Actor 的代码必须声明来源系统
 */

#include "core/Engine.h"
#include "core/DebugManager.h"
#include "audio/AudioSystem.h"
#include "ui/UISystem.h"
#include "scene/Scene.h"
#include "scene/SceneAssetLoader.h"
#include <imgui.h>
#include <imgui_impl_win32.h>

// 核心组件
#include "scene/components/TransformComponent.h"
#include "graphics/components/RenderableComponent.h"
#include "graphics/components/RenderPriorityComponent.h"
#include "graphics/components/CameraComponent.h"
#include "graphics/components/FreeCameraComponent.h"
#include "graphics/components/MeshComponent.h"

// 玩家组件
#include "gameplay/components/PlayerInputComponent.h"
#include "gameplay/components/PlayerComponent.h"
#include "gameplay/components/CharacterControllerComponent.h"
#include "gameplay/components/SpacecraftComponent.h"

// 物理组件
#include "physics/components/GravitySourceComponent.h"
#include "physics/components/GravityAffectedComponent.h"
#include "physics/components/RigidBodyComponent.h"
#include "physics/components/SectorComponent.h"
#include "physics/components/OrbitComponent.h"

// 资源
#include "graphics/resources/Material.h"
#include "graphics/resources/Shader.h"
#include "graphics/resources/TextureLoader.h"
#include "graphics/resources/AssimpLoader.h"
#include "graphics/RenderSystem.h"
#include "physics/PhysXManager.h"
#include <PxPhysicsAPI.h>

#include <Windows.h>
#include <iostream>
#include <sstream>
#include <DbgHelp.h>

#pragma comment(lib, "DbgHelp.lib")

// Forward declare ImGui's Win32 message handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 崩溃处理器
LONG WINAPI CrashHandler(EXCEPTION_POINTERS* pExceptionInfo) {
    DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;
    void* addr = pExceptionInfo->ExceptionRecord->ExceptionAddress;
    
    std::cerr << "\n========== CRASH ==========" << std::endl;
    std::cerr << "Code: 0x" << std::hex << code << std::dec << std::endl;
    std::cerr << "Address: " << addr << std::endl;
    
    if (code == EXCEPTION_ACCESS_VIOLATION && pExceptionInfo->ExceptionRecord->NumberParameters >= 2) {
        std::cerr << "Operation: " << (pExceptionInfo->ExceptionRecord->ExceptionInformation[0] ? "Write" : "Read") << std::endl;
        std::cerr << "Target: 0x" << std::hex << pExceptionInfo->ExceptionRecord->ExceptionInformation[1] << std::dec << std::endl;
    }
    std::cerr << "===========================" << std::endl;
    
    return EXCEPTION_CONTINUE_SEARCH;
}

// 创建调试控制台
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
    SetUnhandledExceptionFilter(CrashHandler);
    CreateDebugConsole();
    
    std::cout << "[Main] Application starting..." << std::endl;
    outer_wilds::DebugManager::GetInstance().Log("Main", "Debug Console Initialized.");

    const int WINDOW_WIDTH = 1280;
    const int WINDOW_HEIGHT = 720;

    HINSTANCE hInstance = GetModuleHandle(NULL);
    HWND hwnd = CreateAppWindow(hInstance, WINDOW_WIDTH, WINDOW_HEIGHT);
    if (hwnd == NULL) {
        outer_wilds::DebugManager::GetInstance().Log("Main", "Failed to create window.");
        return 0;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);

    // Initialize engine
    outer_wilds::Engine& engine = outer_wilds::Engine::GetInstance();
    
    if (engine.Initialize(hwnd, WINDOW_WIDTH, WINDOW_HEIGHT)) {
        outer_wilds::DebugManager::GetInstance().Log("Main", "Engine initialized successfully");
        outer_wilds::DebugManager::GetInstance().SetShowFPS(true);
        
        auto scene = engine.GetSceneManager()->GetActiveScene();
        auto& physxManager = outer_wilds::PhysXManager::GetInstance();
        
        // 获取 D3D11 Device 用于加载模型
        auto* renderBackend = engine.GetRenderSystem()->GetBackend();
        auto* device = renderBackend->GetDevice();
        
        // ========================================
        // 【完整测试场景】
        // 创建星球（带模型）、玩家、飞船
        // 使用 SectorPhysicsSystem 管理扇区内物理
        // ========================================
        std::cout << "\n=== Creating Full Test Scene ===" << std::endl;
        
        auto* pxPhysics = physxManager.GetPhysics();
        auto* pxScene = physxManager.GetScene();
        
        // 【重要】关闭全局重力，由 SectorPhysicsSystem 管理
        pxScene->setGravity(physx::PxVec3(0.0f, 0.0f, 0.0f));
        std::cout << "[Scene] Global gravity disabled (managed by SectorPhysicsSystem)" << std::endl;
        
        const float PLANET_RADIUS = 64.0f;
        const float SURFACE_GRAVITY = 9.8f;
        
        // ========================================
        // 1. 创建星球实体（扇区 + 重力源 + PhysX 地面 + 模型）
        // 注：planet_earth.obj 是单位球（半径1），需要缩放到64米
        // ========================================
        std::cout << "\n=== Loading Earth Model ===" << std::endl;
        auto planetEntity = outer_wilds::SceneAssetLoader::LoadModelAsEntity(
            scene->GetRegistry(), scene, device,
            "C:\\Users\\kkakk\\homework\\OuterWilds\\assets\\BlendObj\\planet_earth.obj",
            "C:\\Users\\kkakk\\homework\\OuterWilds\\assets\\Texture\\plastered_stone_wall_diff_2k.jpg",
            DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
            DirectX::XMFLOAT3(PLANET_RADIUS, PLANET_RADIUS, PLANET_RADIUS)  // 缩放到半径64米
        );
        std::cout << "[Earth] Entity created: " << (planetEntity != entt::null ? "SUCCESS" : "FAILED") << std::endl;
        
        if (planetEntity != entt::null) {
            // SectorComponent（扇区定义）
            auto& sectorComp = scene->GetRegistry().emplace<outer_wilds::components::SectorComponent>(planetEntity);
            sectorComp.name = "TestPlanet";
            sectorComp.worldPosition = { 0.0f, 0.0f, 0.0f };
            sectorComp.worldRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
            sectorComp.influenceRadius = PLANET_RADIUS * 3.0f;
            sectorComp.priority = 0;
            sectorComp.isActive = true;
            
            // OrbitComponent（轨道 - 围绕原点公转）
            auto& orbitComp = scene->GetRegistry().emplace<outer_wilds::components::OrbitComponent>(planetEntity);
            orbitComp.orbitCenter = { 0.0f, 0.0f, 0.0f };       // 围绕原点公转
            orbitComp.orbitRadius = 200.0f;                      // 轨道半径 200m
            orbitComp.orbitPeriod = 60.0f;                       // 公转周期 60秒
            orbitComp.orbitAngle = 0.0f;                         // 初始角度
            orbitComp.orbitNormal = { 0.0f, 1.0f, 0.0f };       // 在XZ平面公转
            orbitComp.orbitEnabled = true;
            // 暂时不启用自转，先测试公转
            orbitComp.rotationEnabled = false;
            orbitComp.rotationPeriod = 30.0f;                    // 自转周期 30秒
            
            // GravitySourceComponent（重力源）
            auto& gravitySource = scene->GetRegistry().emplace<outer_wilds::components::GravitySourceComponent>(planetEntity);
            gravitySource.radius = PLANET_RADIUS;
            gravitySource.surfaceGravity = SURFACE_GRAVITY;
            gravitySource.atmosphereHeight = PLANET_RADIUS * 0.5f;
            gravitySource.isActive = true;
            gravitySource.useRealisticGravity = false;
            
            // PhysX 静态球体（星球表面）
            // [来源: main.cpp 场景初始化]
            physx::PxMaterial* planetMaterial = pxPhysics->createMaterial(0.5f, 0.5f, 0.3f);
            physx::PxTransform planetPose(physx::PxVec3(0.0f, 0.0f, 0.0f));
            physx::PxRigidStatic* planetActor = pxPhysics->createRigidStatic(planetPose);
            
            physx::PxShape* planetShape = physx::PxRigidActorExt::createExclusiveShape(
                *planetActor,
                physx::PxSphereGeometry(PLANET_RADIUS),
                *planetMaterial
            );
            planetShape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE, true);
            planetShape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, true);
            
            pxScene->addActor(*planetActor);
            sectorComp.physxGround = planetActor;
            
            std::cout << "[Planet] Created with model: radius=" << PLANET_RADIUS 
                      << "m, gravity=" << SURFACE_GRAVITY << "m/s^2" << std::endl;
        } else {
            std::cout << "[Planet] ERROR: Failed to load model!" << std::endl;
        }
        
        // ========================================
        // 2. 创建玩家实体（表面行走角色控制器）
        // ========================================
        const float PLAYER_HEIGHT = PLANET_RADIUS + 3.0f;  // 站在星球表面上方
        auto playerEntity = outer_wilds::SceneAssetLoader::LoadModelAsEntity(
            scene->GetRegistry(), scene, device,
            "C:\\Users\\kkakk\\homework\\OuterWilds\\assets\\BlendObj\\player.obj",
            "",
            DirectX::XMFLOAT3(0.0f, PLAYER_HEIGHT, 0.0f),
            DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f)
        );
        
        if (playerEntity != entt::null) {
            // InSectorComponent（标记属于星球扇区）
            auto& inSector = scene->GetRegistry().emplace<outer_wilds::components::InSectorComponent>(playerEntity);
            inSector.sector = planetEntity;
            inSector.localPosition = { 0.0f, PLAYER_HEIGHT, 0.0f };
            inSector.localRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
            inSector.isInitialized = true;
            
            // GravityAffectedComponent（受重力影响）
            auto& gravityAffected = scene->GetRegistry().emplace<outer_wilds::components::GravityAffectedComponent>(playerEntity);
            gravityAffected.affectedByGravity = true;
            gravityAffected.gravityScale = 1.0f;
            gravityAffected.currentGravitySource = planetEntity;
            
            // PlayerInputComponent（接收输入）
            auto& playerInput = scene->GetRegistry().emplace<outer_wilds::PlayerInputComponent>(playerEntity);
            playerInput.mouseLookEnabled = false;  // 初始不锁定鼠标
            
            // PlayerComponent（标记为玩家）
            scene->GetRegistry().emplace<outer_wilds::PlayerComponent>(playerEntity);
            
            // PlayerSpacecraftInteractionComponent（飞船交互）
            scene->GetRegistry().emplace<outer_wilds::components::PlayerSpacecraftInteractionComponent>(playerEntity);
            
            // CameraComponent（玩家第一人称相机）
            auto& playerCamera = scene->GetRegistry().emplace<outer_wilds::components::CameraComponent>(playerEntity);
            playerCamera.isActive = true;  // 玩家相机初始激活
            playerCamera.fov = 75.0f;
            playerCamera.aspectRatio = 16.0f / 9.0f;
            playerCamera.nearPlane = 0.1f;
            playerCamera.farPlane = 2000.0f;
            playerCamera.position = { 0.0f, PLAYER_HEIGHT + 0.8f, 0.0f };  // 眼睛位置略高于胶囊中心
            playerCamera.target = { 0.0f, PLAYER_HEIGHT + 0.8f, -1.0f };
            playerCamera.up = { 0.0f, 1.0f, 0.0f };
            playerCamera.moveSpeed = 6.0f;
            playerCamera.lookSensitivity = 0.003f;
            playerCamera.yaw = 0.0f;
            playerCamera.pitch = 0.0f;
            
            // CharacterControllerComponent（表面行走控制器）
            auto& character = scene->GetRegistry().emplace<outer_wilds::components::CharacterControllerComponent>(playerEntity);
            character.moveSpeed = 12.0f;   // 步行速度
            character.runSpeed = 24.0f;    // 跑步速度 (按住Shift)
            character.jumpForce = 10.0f;
            character.capsuleRadius = 0.4f;
            character.capsuleHalfHeight = 0.9f;
            character.maxSlopeAngle = 50.0f;
            character.stepOffset = 0.3f;
            
            // 创建 PxCapsuleController（用于球面行走）
            // [来源: main.cpp 场景初始化]
            physx::PxMaterial* playerMaterial = pxPhysics->createMaterial(0.5f, 0.5f, 0.1f);
            
            physx::PxCapsuleControllerDesc controllerDesc;
            controllerDesc.height = character.capsuleHalfHeight * 2.0f;  // 总高度
            controllerDesc.radius = character.capsuleRadius;
            controllerDesc.material = playerMaterial;
            controllerDesc.position = physx::PxExtendedVec3(0.0f, PLAYER_HEIGHT, 0.0f);
            controllerDesc.slopeLimit = std::cos(DirectX::XMConvertToRadians(character.maxSlopeAngle));
            controllerDesc.stepOffset = character.stepOffset;
            controllerDesc.contactOffset = 0.2f;  // 增大接触偏移，防止穿透
            controllerDesc.upDirection = physx::PxVec3(0.0f, 1.0f, 0.0f);  // 初始up方向
            controllerDesc.climbingMode = physx::PxCapsuleClimbingMode::eCONSTRAINED;
            controllerDesc.nonWalkableMode = physx::PxControllerNonWalkableMode::ePREVENT_CLIMBING_AND_FORCE_SLIDING;  // 滑落不可行走坡度
            
            auto* controllerManager = physxManager.GetControllerManager();
            character.pxController = controllerManager->createController(controllerDesc);
            
            if (character.pxController) {
                std::cout << "[Player] Created PxCapsuleController at (0, " << PLAYER_HEIGHT << ", 0)" << std::endl;
            } else {
                std::cout << "[Player] ERROR: Failed to create PxCapsuleController!" << std::endl;
            }
            
            std::cout << "[Player] Created at (0, " << PLAYER_HEIGHT << ", 0) with surface walking physics" << std::endl;
        } else {
            std::cout << "[Player] ERROR: Failed to load model!" << std::endl;
        }
        
#if 0  // 暂时禁用飞船加载，加速调试
        // ========================================
        // 3. 创建飞船实体（在星球表面）
        // 飞船模型原始尺寸很大，需要缩小到5%
        // ========================================
        const float SPACECRAFT_SCALE = 0.05f;  // 5%缩放
        const float SPACECRAFT_HALF_HEIGHT = 1.0f * SPACECRAFT_SCALE;  // 飞船高度的一半
        const float SPACECRAFT_HEIGHT = PLANET_RADIUS + SPACECRAFT_HALF_HEIGHT + 0.5f;  // 放在星球表面
        const float SPACECRAFT_X_OFFSET = 15.0f;  // X方向偏移
        
        auto spacecraftEntity = outer_wilds::SceneAssetLoader::LoadModelAsEntity(
            scene->GetRegistry(), scene, device,
            "C:\\Users\\kkakk\\homework\\OuterWilds\\assets\\models\\spacecraft\\base_basic_pbr.fbx",
            "C:\\Users\\kkakk\\homework\\OuterWilds\\assets\\models\\spacecraft\\texture_diffuse_00.png",
            DirectX::XMFLOAT3(SPACECRAFT_X_OFFSET, SPACECRAFT_HEIGHT, 0.0f),
            DirectX::XMFLOAT3(SPACECRAFT_SCALE, SPACECRAFT_SCALE, SPACECRAFT_SCALE)
        );
        
        if (spacecraftEntity != entt::null) {
            // InSectorComponent（标记属于星球扇区）
            auto& inSector = scene->GetRegistry().emplace<outer_wilds::components::InSectorComponent>(spacecraftEntity);
            inSector.sector = planetEntity;
            inSector.localPosition = { SPACECRAFT_X_OFFSET, SPACECRAFT_HEIGHT, 0.0f };
            inSector.localRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
            inSector.isInitialized = true;
            
            // GravityAffectedComponent（受重力影响）
            auto& gravityAffected = scene->GetRegistry().emplace<outer_wilds::components::GravityAffectedComponent>(spacecraftEntity);
            gravityAffected.affectedByGravity = true;
            gravityAffected.gravityScale = 1.0f;
            gravityAffected.currentGravitySource = planetEntity;
            
            // RigidBodyComponent（刚体属性）
            auto& rigidBody = scene->GetRegistry().emplace<outer_wilds::RigidBodyComponent>(spacecraftEntity);
            rigidBody.mass = 500.0f;  // 飞船较重
            rigidBody.drag = 0.5f;
            rigidBody.angularDrag = 5.0f;  // 高角阻尼防止快速旋转
            rigidBody.useGravity = true;
            rigidBody.isKinematic = false;
            
            // SpacecraftComponent（飞船属性）
            auto& spacecraft = scene->GetRegistry().emplace<outer_wilds::components::SpacecraftComponent>(spacecraftEntity);
            spacecraft.currentState = outer_wilds::components::SpacecraftComponent::State::IDLE;
            spacecraft.mainThrust = 15000.0f;
            spacecraft.mass = 500.0f;
            
            // PhysX 动态盒子（飞船碰撞）
            // 飞船碰撞盒尺寸需要匹配 scale=2 的模型
            // [来源: main.cpp 场景初始化]
            const float BOX_HALF_X = 2.0f * SPACECRAFT_SCALE;  // 宽度
            const float BOX_HALF_Y = 1.0f * SPACECRAFT_SCALE;  // 高度
            const float BOX_HALF_Z = 3.0f * SPACECRAFT_SCALE;  // 长度
            
            // 材质：高摩擦、零弹性，防止弹跳和滑动
            physx::PxMaterial* spacecraftMaterial = pxPhysics->createMaterial(0.8f, 0.8f, 0.0f);
            physx::PxTransform spacecraftPose(physx::PxVec3(SPACECRAFT_X_OFFSET, SPACECRAFT_HEIGHT, 0.0f));
            physx::PxRigidDynamic* spacecraftActor = pxPhysics->createRigidDynamic(spacecraftPose);
            
            // 使用盒子作为飞船碰撞形状
            physx::PxShape* spacecraftShape = physx::PxRigidActorExt::createExclusiveShape(
                *spacecraftActor,
                physx::PxBoxGeometry(BOX_HALF_X, BOX_HALF_Y, BOX_HALF_Z),
                *spacecraftMaterial
            );
            spacecraftShape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE, true);
            spacecraftShape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, true);
            
            physx::PxRigidBodyExt::updateMassAndInertia(*spacecraftActor, 1.0f);
            spacecraftActor->setMass(rigidBody.mass);
            spacecraftActor->setLinearDamping(2.0f);   // 高线性阻尼
            spacecraftActor->setAngularDamping(10.0f); // 很高的角阻尼
            spacecraftActor->setMaxLinearVelocity(200.0f);
            spacecraftActor->setMaxAngularVelocity(1.0f);  // 更低的最大角速度
            spacecraftActor->setSolverIterationCounts(16, 8);  // 更多迭代提高稳定性
            
            // 设置睡眠阈值，让飞船更快进入静止状态
            spacecraftActor->setSleepThreshold(0.5f);
            
            pxScene->addActor(*spacecraftActor);
            spacecraftActor->wakeUp();
            rigidBody.physxActor = spacecraftActor;
            
            std::cout << "[Spacecraft] Created at (" << SPACECRAFT_X_OFFSET << ", " << SPACECRAFT_HEIGHT << ", 0)" 
                      << " with collision box (" << BOX_HALF_X*2 << "x" << BOX_HALF_Y*2 << "x" << BOX_HALF_Z*2 << ")" << std::endl;
        } else {
            std::cout << "[Spacecraft] ERROR: Failed to load model!" << std::endl;
        }
#endif  // 暂时禁用飞船
        
        // ========================================
        // 4. 创建太阳（轨道中心，GLB模型，带发光材质）
        // ========================================
        std::cout << "\n=== Loading Sun (GLB with Emissive) ===" << std::endl;
        entt::entity sunEntity = entt::null;
        {
            // 先加载模型获取尺寸信息
            outer_wilds::resources::LoadedModel sunModel;
            if (outer_wilds::resources::AssimpLoader::LoadFromFile(
                "C:\\Users\\kkakk\\homework\\OuterWilds\\assets\\models\\sun\\sun_final.glb", sunModel)) {
                
                std::cout << "[Sun] Model bounds: " 
                          << sunModel.bounds.size.x << " x " 
                          << sunModel.bounds.size.y << " x " 
                          << sunModel.bounds.size.z << std::endl;
                std::cout << "[Sun] Bounding sphere radius: " << sunModel.bounds.radius << std::endl;
            }
            
            // 太阳缩放：设置太阳半径约为50米（比地球小一些，方便观察）
            const float SUN_SCALE = 25.0f;  // 调整这个值来改变太阳大小
            
            // 太阳位于轨道中心
            sunEntity = outer_wilds::SceneAssetLoader::LoadModelAsEntity(
                scene->GetRegistry(), scene, device,
                "C:\\Users\\kkakk\\homework\\OuterWilds\\assets\\models\\sun\\sun_final.glb",
                "",  // 使用GLB嵌入式纹理（包含发光贴图）
                DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),  // 轨道中心
                DirectX::XMFLOAT3(SUN_SCALE, SUN_SCALE, SUN_SCALE)   // 缩放到合适大小
            );
            
            if (sunEntity != entt::null) {
                // 给太阳添加自转
                auto& sunOrbit = scene->GetRegistry().emplace<outer_wilds::components::OrbitComponent>(sunEntity);
                sunOrbit.orbitEnabled = false;           // 太阳不公转（它是中心）
                sunOrbit.rotationEnabled = true;         // 太阳自转
                sunOrbit.rotationPeriod = 60.0f;         // 自转周期60秒
                sunOrbit.rotationAxis = { 0.0f, 1.0f, 0.0f };
                
                // 设置太阳实体给渲染系统（用于动态光照）
                engine.GetRenderSystem()->SetSunEntity(sunEntity);
                
                std::cout << "[Sun] Created at orbit center (0, 0, 0) with rotation" << std::endl;
            } else {
                std::cout << "[Sun] ERROR: Failed to load GLB model!" << std::endl;
            }
        }
        
        // ========================================
        // 5. 创建月球（围绕地球的轨道，使用OBJ模型）
        // ========================================
        std::cout << "\n=== Loading Moon (OBJ with Earth Orbit) ===" << std::endl;
        {
            // 月球围绕地球的轨道参数
            const float MOON_ORBIT_RADIUS = 120.0f;  // 月球到地球的距离（地球半径64m，月球轨道120m比较合适）
            const float MOON_ORBIT_PERIOD = 45.0f;   // 公转周期（秒）- 比地球公转快
            const float MOON_SCALE = 1.5f;           // 月球缩放（比地球小，约1/4大小）
            
            // 初始位置需要考虑地球的初始位置（地球初始在X=200处，因为角度为0）
            const float EARTH_INITIAL_X = 200.0f;    // 地球轨道半径
            
            auto moonEntity = outer_wilds::SceneAssetLoader::LoadModelAsEntity(
                scene->GetRegistry(), scene, device,
                "C:\\Users\\kkakk\\homework\\OuterWilds\\assets\\BlendObj\\planet_earth.obj",  // 使用新的球体模型
                "C:\\Users\\kkakk\\homework\\OuterWilds\\assets\\Texture\\plastered_stone_wall_diff_2k.jpg",  // 使用纹理
                DirectX::XMFLOAT3(EARTH_INITIAL_X + MOON_ORBIT_RADIUS, 0.0f, 0.0f),  // 初始位置（地球右侧）
                DirectX::XMFLOAT3(MOON_SCALE, MOON_SCALE, MOON_SCALE)
            );
            
            if (moonEntity != entt::null) {
                // OrbitComponent（月球轨道 - 围绕地球公转！）
                auto& orbitComp = scene->GetRegistry().emplace<outer_wilds::components::OrbitComponent>(moonEntity);
                orbitComp.orbitParent = planetEntity;                   // 【关键】围绕地球公转！
                orbitComp.orbitCenter = { EARTH_INITIAL_X, 0.0f, 0.0f }; // 初始中心（会被orbitParent覆盖）
                orbitComp.orbitRadius = MOON_ORBIT_RADIUS;              // 轨道半径（离地球的距离）
                orbitComp.orbitPeriod = MOON_ORBIT_PERIOD;              // 公转周期
                orbitComp.orbitAngle = 0.0f;                            // 初始角度
                orbitComp.orbitNormal = { 0.0f, 1.0f, 0.0f };          // 在XZ平面公转
                orbitComp.orbitInclination = 0.1f;                      // 轻微轨道倾斜（更真实）
                orbitComp.orbitEnabled = true;
                orbitComp.rotationEnabled = true;                       // 月球潮汐锁定（自转=公转周期）
                orbitComp.rotationPeriod = MOON_ORBIT_PERIOD;           // 自转周期与公转同步
                
                std::cout << "[Moon] Created orbiting Earth: radius=" << MOON_ORBIT_RADIUS 
                          << "m, period=" << MOON_ORBIT_PERIOD << "s, scale=" << MOON_SCALE << std::endl;
            } else {
                std::cout << "[Moon] ERROR: Failed to load OBJ model!" << std::endl;
            }
        }
        
        std::cout << "\n=== Full Test Scene Ready ===" << std::endl;
        std::cout << "Controls: WASD=Move, Mouse=Look, Shift=Fast, Space=Up, Ctrl=Down" << std::endl;
        std::cout << "ESC+Backspace = Toggle mouse lock, Shift+ESC = Exit" << std::endl;
        std::cout << std::endl;
        
        // ========================================
        // 启动欢迎界面流程
        // ========================================
        if (auto uiSystem = engine.GetUISystem()) {
            uiSystem->ShowWelcomeScreenWithKeyWait("C:\\Users\\kkakk\\homework\\OuterWilds\\assets\\ui\\kkstudio1.jpg");
        }
        
        if (auto audioSystem = engine.GetAudioSystem()) {
            audioSystem->PlaySingleTrack("C:\\Users\\kkakk\\homework\\OuterWilds\\assets\\Outer Wilds (Original Soundtrack)\\02 - Outer Wilds.mp3");
        }
        
        // 欢迎界面循环
        bool shouldExit = false;
        while (engine.GetUISystem() && engine.GetUISystem()->IsWelcomeScreenVisible() && !shouldExit) {
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
            
            outer_wilds::TimeManager::GetInstance().Update();
            float deltaTime = outer_wilds::TimeManager::GetInstance().GetDeltaTime();
            
            auto& registry = scene->GetRegistry();
            if (auto uiSys = engine.GetUISystem()) {
                uiSys->Update(deltaTime, registry);
            }
            if (auto audioSys = engine.GetAudioSystem()) {
                audioSys->Update(deltaTime, registry);
            }
            
            // 渲染
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
            
            if (engine.GetUISystem() && engine.GetUISystem()->WasKeyPressed()) {
                break;
            }
        }
        
        if (shouldExit) {
            engine.Shutdown();
            return 0;
        }
        
        // 切换到游戏音乐
        if (auto audioSystem = engine.GetAudioSystem()) {
            std::string musicFolder = "C:\\Users\\kkakk\\homework\\OuterWilds\\assets\\Outer Wilds (Original Soundtrack)";
            if (audioSystem->LoadPlaylistFromDirectory(musicFolder)) {
                audioSystem->SetLoopPlaylist(true);
                audioSystem->Play();
            }
        }
        
        // 主游戏循环
        outer_wilds::DebugManager::GetInstance().Log("Main", "Entering main game loop");
        
        try {
            engine.Run();
        } catch (const std::exception& e) {
            std::cerr << "[CRASH] Exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[CRASH] Unknown exception!" << std::endl;
        }
        
        std::cout << "[Main] Engine.Run() returned" << std::endl;
    }
    
    engine.Shutdown();
    std::cout << "[Main] Program exiting normally." << std::endl;
    return 0;
}
