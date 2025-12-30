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
#include "scene/SolarSystemConfig.h"
#include "scene/SolarSystemBuilder.h"
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
        // 【微缩太阳系场景】
        // 使用 SolarSystemBuilder 创建完整太阳系
        // ========================================
        std::cout << "\n=== Creating Miniature Solar System ===" << std::endl;
        
        auto* pxPhysics = physxManager.GetPhysics();
        auto* pxScene = physxManager.GetScene();
        
        // 【重要】关闭全局重力，由 SectorPhysicsSystem 管理
        pxScene->setGravity(physx::PxVec3(0.0f, 0.0f, 0.0f));
        std::cout << "[Scene] Global gravity disabled (managed by SectorPhysicsSystem)" << std::endl;
        
        // 使用太阳系构建器创建所有天体
        const std::string ASSETS_BASE = "C:\\Users\\kkakk\\homework\\OuterWilds";
        auto solarSystem = outer_wilds::SolarSystemBuilder::Build(
            scene->GetRegistry(), scene, device, ASSETS_BASE
        );
        
        // 设置太阳实体给渲染系统（用于动态光照）
        if (solarSystem.sun != entt::null) {
            engine.GetRenderSystem()->SetSunEntity(solarSystem.sun);
        }
        
        // 获取地球实体（用于玩家出生点）
        auto planetEntity = solarSystem.earth;
        const float PLANET_RADIUS = outer_wilds::SolarSystemConfig::EARTH_RADIUS;  // 现在是 64m
        const float SURFACE_GRAVITY = outer_wilds::SolarSystemConfig::EARTH_GRAVITY;
        
        std::cout << "[Main] Using Earth: entity=" << static_cast<uint32_t>(planetEntity) 
                  << ", PLANET_RADIUS=" << PLANET_RADIUS << std::endl;
        
        if (planetEntity == entt::null) {
            std::cout << "[ERROR] Earth not created, falling back to default spawn" << std::endl;
        }
        
        // ========================================
        // 2. 创建玩家实体（表面行走角色控制器）
        // ========================================
        // 【重要】玩家位置使用扇区局部坐标系
        // 局部坐标：相对于地球中心（原点）的位置
        // 玩家在"北极"上方，即 Y 轴正方向
        const float PLAYER_LOCAL_HEIGHT = PLANET_RADIUS + 3.0f;  // 站在星球表面上方
        
        std::cout << "\n[Main] === Player Setup ===" << std::endl;
        std::cout << "[Main] PLANET_RADIUS = " << PLANET_RADIUS << std::endl;
        std::cout << "[Main] PLAYER_LOCAL_HEIGHT = " << PLAYER_LOCAL_HEIGHT << std::endl;
        
        // 使用多材质加载器支持 FBX 模型
        auto playerEntity = outer_wilds::SceneAssetLoader::LoadMultiMaterialModelAsEntities(
            scene->GetRegistry(), scene, device,
            "C:\\Users\\kkakk\\homework\\OuterWilds\\assets\\models\\human\\extracted\\models\\SK_SciFiTrooperManV3.fbx",
            "",  // 贴图和模型在同一目录
            DirectX::XMFLOAT3(0.0f, PLAYER_LOCAL_HEIGHT, 0.0f),  // 局部坐标
            DirectX::XMFLOAT3(0.01f, 0.01f, 0.01f)  // FBX 模型通常需要缩小
        );
        
        std::cout << "[Main] Player entity ID: " << static_cast<uint32_t>(playerEntity) << std::endl;
        
        if (playerEntity != entt::null) {
            // 检查模型加载后的 TransformComponent
            auto* initTransform = scene->GetRegistry().try_get<outer_wilds::TransformComponent>(playerEntity);
            if (initTransform) {
                std::cout << "[Main] Player model loaded at position: (" 
                          << initTransform->position.x << ", " << initTransform->position.y 
                          << ", " << initTransform->position.z << ")" << std::endl;
            }
            
            // InSectorComponent（标记属于星球扇区）
            // 【关键】sector 指向地球，localPosition 是相对于地球中心的位置
            auto& inSector = scene->GetRegistry().emplace<outer_wilds::components::InSectorComponent>(playerEntity);
            inSector.sector = planetEntity;
            inSector.localPosition = { 0.0f, PLAYER_LOCAL_HEIGHT, 0.0f };  // 局部坐标
            inSector.localRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
            inSector.isInitialized = true;
            
            std::cout << "[Main] Player InSectorComponent set: localPosition=(" 
                      << inSector.localPosition.x << ", " << inSector.localPosition.y 
                      << ", " << inSector.localPosition.z << "), sector=" 
                      << static_cast<uint32_t>(inSector.sector) << std::endl;
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
            playerCamera.fov = 60.0f;      // 降低FOV，让脚下地面更容易看到
            playerCamera.aspectRatio = 16.0f / 9.0f;
            playerCamera.nearPlane = 0.1f;
            playerCamera.farPlane = 50000.0f;  // 保持远裁剪面以看到整个太阳系
            // 相机初始位置（使用局部坐标）
            playerCamera.position = { 0.0f, PLAYER_LOCAL_HEIGHT + 0.8f, 0.0f };  // 眼睛位置
            playerCamera.target = { 0.0f, PLAYER_LOCAL_HEIGHT + 0.8f, -1.0f };
            playerCamera.up = { 0.0f, 1.0f, 0.0f };
            playerCamera.moveSpeed = 6.0f;
            playerCamera.lookSensitivity = 0.003f;
            playerCamera.yaw = 0.0f;
            playerCamera.pitch = 0.0f;
            
            // CharacterControllerComponent（表面行走控制器）
            auto& character = scene->GetRegistry().emplace<outer_wilds::components::CharacterControllerComponent>(playerEntity);
            character.moveSpeed = 12.0f;   // 步行速度
            character.runSpeed = 24.0f;    // 跑步速度 (按住Shift)
            character.jumpForce = 8.0f;
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
            // 【关键】PhysX Controller 使用局部坐标（相对于扇区原点）
            // 因为地球 PhysX 碰撞体也在原点
            controllerDesc.position = physx::PxExtendedVec3(
                0.0f, 
                PLAYER_LOCAL_HEIGHT, 
                0.0f
            );
            controllerDesc.slopeLimit = std::cos(DirectX::XMConvertToRadians(character.maxSlopeAngle));
            controllerDesc.stepOffset = character.stepOffset;
            controllerDesc.contactOffset = 0.2f;  // 增大接触偏移，防止穿透
            controllerDesc.upDirection = physx::PxVec3(0.0f, 1.0f, 0.0f);  // 初始up方向
            controllerDesc.climbingMode = physx::PxCapsuleClimbingMode::eCONSTRAINED;
            controllerDesc.nonWalkableMode = physx::PxControllerNonWalkableMode::ePREVENT_CLIMBING_AND_FORCE_SLIDING;  // 滑落不可行走坡度
            
            auto* controllerManager = physxManager.GetControllerManager();
            character.pxController = controllerManager->createController(controllerDesc);
            
            if (character.pxController) {
                // 验证 PhysX Controller 的实际位置
                physx::PxExtendedVec3 actualPos = character.pxController->getPosition();
                std::cout << "[Player] Created PxCapsuleController, requested pos: (0, " 
                          << PLAYER_LOCAL_HEIGHT << ", 0), actual pos: (" 
                          << actualPos.x << ", " << actualPos.y << ", " << actualPos.z
                          << ")" << std::endl;
            } else {
                std::cout << "[Player] ERROR: Failed to create PxCapsuleController!" << std::endl;
            }
            // 确保相机在正确的眼睛位置
            // 【注意】相机位置使用局部坐标，会由 SectorPhysicsSystem 转换到世界坐标
            {
                if (scene->GetRegistry().all_of<outer_wilds::components::CameraComponent>(playerEntity) &&
                    scene->GetRegistry().all_of<outer_wilds::components::CharacterControllerComponent>(playerEntity)) {
                    auto& cam = scene->GetRegistry().get<outer_wilds::components::CameraComponent>(playerEntity);
                    auto& ctrl = scene->GetRegistry().get<outer_wilds::components::CharacterControllerComponent>(playerEntity);
                    // 相机位置 = 玩家脚底 + 眼睛高度
                    cam.position = { 
                        0.0f, 
                        PLAYER_LOCAL_HEIGHT + ctrl.cameraOffset.y, 
                        0.0f 
                    };
                    cam.target = { 
                        0.0f, 
                        PLAYER_LOCAL_HEIGHT + ctrl.cameraOffset.y, 
                        -1.0f 
                    };
                }
            }
            
            std::cout << "[Player] Created at LOCAL (0, " 
                      << PLAYER_LOCAL_HEIGHT << ", 0) with surface walking physics" << std::endl;
        } else {
            std::cout << "[Player] ERROR: Failed to load model!" << std::endl;
        }
        

        // ========================================
        // 3. 创建飞船实体（在星球表面）
        // 【注意】FBX模型通常是厘米为单位，需要缩放5%转换为米
        // 但碰撞盒不能太小，否则物理模拟不稳定，所以碰撞盒使用固定尺寸
        // ========================================
        const float SPACECRAFT_SCALE = 0.05f;  // 5%缩放（FBX厘米转米）
        // 飞船原始高度约20m，缩放后约1m
        const float SPACECRAFT_VISUAL_HEIGHT = 1.0f;  // 飞船视觉高度（缩放后）
        const float SPACECRAFT_LOCAL_HEIGHT = PLANET_RADIUS + SPACECRAFT_VISUAL_HEIGHT + 0.5f;  // 放在星球表面
        const float SPACECRAFT_LOCAL_X_OFFSET = 15.0f;  // X方向偏移（局部坐标）
        
        // 【优化】使用带选项的加载方法，跳过包围盒计算以加速加载
        outer_wilds::ModelLoadingOptions spacecraftLoadOptions;
        spacecraftLoadOptions.skipBoundsCalculation = true;  // 飞船使用固定scale，无需计算包围盒
        spacecraftLoadOptions.fastLoad = true;               // 【快速加载】跳过切线/法线生成
        spacecraftLoadOptions.verbose = true;
        
        auto spacecraftEntity = outer_wilds::SceneAssetLoader::LoadModelAsEntityWithOptions(
            scene->GetRegistry(), scene, device,
            "C:\\Users\\kkakk\\homework\\OuterWilds\\assets\\models\\spacecraft\\base_basic_pbr.fbx",
            "C:\\Users\\kkakk\\homework\\OuterWilds\\assets\\models\\spacecraft\\texture_diffuse_00.png",
            DirectX::XMFLOAT3(SPACECRAFT_LOCAL_X_OFFSET, SPACECRAFT_LOCAL_HEIGHT, 0.0f),  // 局部坐标
            DirectX::XMFLOAT3(SPACECRAFT_SCALE, SPACECRAFT_SCALE, SPACECRAFT_SCALE),
            spacecraftLoadOptions
        );
        
        if (spacecraftEntity != entt::null) {
            // InSectorComponent（标记属于星球扇区）
            // 【关键】飞船位置使用局部坐标（相对于地球中心）
            auto& inSector = scene->GetRegistry().emplace<outer_wilds::components::InSectorComponent>(spacecraftEntity);
            inSector.sector = planetEntity;
            inSector.localPosition = { SPACECRAFT_LOCAL_X_OFFSET, SPACECRAFT_LOCAL_HEIGHT, 0.0f };
            
            // 计算飞船朝向：站在星球表面
            // 【重要】FBX 飞船模型的坐标系（标准FBX约定）：
            //   - 模型前方 = +Z 方向（船头朝 +Z）
            //   - 模型上方 = +Y 方向（船顶朝 +Y）
            //   - 模型右方 = +X 方向
            // 
            // 目标世界坐标系：
            //   - 飞船 "上方"（模型+Y）应该指向星球外（surfaceNormal）
            //   - 飞船 "前方"（模型+Z）应该沿着地表切线
            
            // 步骤1：计算 surfaceNormal（指向天空）= normalize(position)
            DirectX::XMVECTOR surfaceNormal = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&inSector.localPosition));
            
            // 步骤2：计算地表切线方向（作为飞船前方）
            DirectX::XMVECTOR worldUp = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            DirectX::XMVECTOR tangent = DirectX::XMVector3Cross(worldUp, surfaceNormal);
            
            float tangentLen = DirectX::XMVectorGetX(DirectX::XMVector3Length(tangent));
            if (tangentLen < 0.001f) {
                tangent = DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
            }
            tangent = DirectX::XMVector3Normalize(tangent);
            
            // 计算第三个轴（右方向）
            // 【关键】右手坐标系：right = up × forward
            DirectX::XMVECTOR rightDir = DirectX::XMVector3Cross(surfaceNormal, tangent);
            rightDir = DirectX::XMVector3Normalize(rightDir);
            
            // 步骤3：构建旋转矩阵
            // 模型坐标系（标准FBX）-> 世界坐标系的映射：
            //   模型 +X (right)   -> rightDir       (飞船右侧)
            //   模型 +Y (up)      -> surfaceNormal  (指向天空)
            //   模型 +Z (forward) -> tangent        (飞船前方)
            DirectX::XMMATRIX rotMatrix;
            rotMatrix.r[0] = DirectX::XMVectorSetW(rightDir, 0.0f);        // X 轴 = 右
            rotMatrix.r[1] = DirectX::XMVectorSetW(surfaceNormal, 0.0f);   // Y 轴 = 上（天空）
            rotMatrix.r[2] = DirectX::XMVectorSetW(tangent, 0.0f);         // Z 轴 = 前
            rotMatrix.r[3] = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
            
            DirectX::XMVECTOR alignQuat = DirectX::XMQuaternionRotationMatrix(rotMatrix);
            DirectX::XMStoreFloat4(&inSector.localRotation, alignQuat);
            inSector.isInitialized = true;
            
            // GravityAffectedComponent（受重力影响）
            auto& gravityAffected = scene->GetRegistry().emplace<outer_wilds::components::GravityAffectedComponent>(spacecraftEntity);
            gravityAffected.affectedByGravity = true;
            gravityAffected.gravityScale = 1.0f;
            gravityAffected.currentGravitySource = planetEntity;
            // 【关键】初始化重力方向和强度，否则第一帧重力为0
            DirectX::XMFLOAT3 gravDir;
            DirectX::XMStoreFloat3(&gravDir, DirectX::XMVectorNegate(surfaceNormal));
            gravityAffected.currentGravityDir = gravDir;
            gravityAffected.currentGravityStrength = SURFACE_GRAVITY;
            
            // RigidBodyComponent（刚体属性）
            auto& rigidBody = scene->GetRegistry().emplace<outer_wilds::RigidBodyComponent>(spacecraftEntity);
            rigidBody.mass = 500.0f;  // 飞船较重
            rigidBody.drag = 0.0f;    // 太空无阻力
            rigidBody.angularDrag = 0.5f;  // 低角阻尼
            rigidBody.useGravity = true;
            rigidBody.isKinematic = false;
            
            // SpacecraftComponent（飞船属性）
            auto& spacecraft = scene->GetRegistry().emplace<outer_wilds::components::SpacecraftComponent>(spacecraftEntity);
            spacecraft.currentState = outer_wilds::components::SpacecraftComponent::State::IDLE;
            spacecraft.mainThrust = 15000.0f;
            spacecraft.mass = 500.0f;
            
            // PhysX 动态盒子（飞船碰撞）
            // 【重要】碰撞盒使用固定尺寸，不跟随模型scale
            // 原因：5cm的碰撞盒会PhysX不稳定，而模型缩放5%后只有约1m大小
            // 所以碰撞盒略大于视觉模型，确保物理稳定
            // 这里故意做得更"扁平宽大"，让着陆/摩擦区域更大、更稳
            // [来源: main.cpp 场景初始化]
            const float BOX_HALF_X = 1.5f;  // 宽度半尺寸 1.5m  → 实宽 3.0m
            const float BOX_HALF_Y = 0.4f;  // 高度半尺寸 0.4m  → 实高 0.8m（更扁） 
            const float BOX_HALF_Z = 2.0f;  // 长度半尺寸 2.0m  → 实长 4.0m
            
            // 材质：高摩擦、零弹性，防止弹跳和滑动
            physx::PxMaterial* spacecraftMaterial = pxPhysics->createMaterial(0.8f, 0.8f, 0.0f);
            // 【关键】PhysX Actor 使用局部坐标（相对于地球扇区原点）
            // 同时设置旋转，使飞船朝向正确
            physx::PxQuat pxRotation(inSector.localRotation.x, inSector.localRotation.y, 
                                     inSector.localRotation.z, inSector.localRotation.w);
            physx::PxTransform spacecraftPose(
                physx::PxVec3(SPACECRAFT_LOCAL_X_OFFSET, SPACECRAFT_LOCAL_HEIGHT, 0.0f),
                pxRotation
            );
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
            // 【物理参数】适当的阻尼，既能在太空保持漂移，又能在接触地面时快速稳定
            spacecraftActor->setLinearDamping(0.2f);   // 轻微线性阻尼，减少地面碰撞后的滑动颤抖
            spacecraftActor->setAngularDamping(2.0f);  // 提高角阻尼，抑制旋转抖动
            spacecraftActor->setMaxLinearVelocity(500.0f);  // 更高最大速度
            spacecraftActor->setMaxAngularVelocity(3.0f);   // 限制最大角速度（防止旋转太快）
            spacecraftActor->setSolverIterationCounts(8, 4);
            
            // 降低睡眠阈值，让飞船在有重力时不会轻易睡眠
            spacecraftActor->setSleepThreshold(0.05f);
            
            pxScene->addActor(*spacecraftActor);
            spacecraftActor->wakeUp();
            rigidBody.physxActor = spacecraftActor;
            
            std::cout << "[Spacecraft] Created at LOCAL (" << SPACECRAFT_LOCAL_X_OFFSET 
                      << ", " << SPACECRAFT_LOCAL_HEIGHT << ", 0)" 
                      << " with collision box (" << BOX_HALF_X*2 << "x" << BOX_HALF_Y*2 << "x" << BOX_HALF_Z*2 << ")"
                      << ", model scale=" << SPACECRAFT_SCALE << " (5%, FBX cm->m)" << std::endl;
        } else {
            std::cout << "[Spacecraft] ERROR: Failed to load model!" << std::endl;
        }

        
        // 太阳系已通过 SolarSystemBuilder 创建

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
