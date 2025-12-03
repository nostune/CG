# OuterWilds ECS 引擎架构文档

## 目录
1. [系统概览](#系统概览)
2. [核心系统详解](#核心系统详解)
3. [主循环流程](#主循环流程)
4. [场景资源加载](#场景资源加载)
5. [关键变量索引](#关键变量索引)
6. [调试指南](#调试指南)

---

## 系统概览

### 架构图
```
┌─────────────────────────────────────────────────────────────┐
│                         Engine                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐     │
│  │ SceneManager │  │ RenderSystem │  │ PhysicsSystem│     │
│  └──────────────┘  └──────────────┘  └──────────────┘     │
│         │                  │                  │             │
│         ▼                  ▼                  ▼             │
│  ┌──────────────────────────────────────────────────┐     │
│  │           ECS Registry (EnTT)                    │     │
│  │   Entities + Components (Transform, Mesh...)     │     │
│  └──────────────────────────────────────────────────┘     │
└─────────────────────────────────────────────────────────────┘
```

### 系统列表
| 系统 | 职责 | 依赖 |
|------|------|------|
| **Engine** | 主循环、系统协调 | 所有系统 |
| **SceneManager** | 场景管理、实体创建 | ECS Registry |
| **RenderSystem** | 渲染队列、绘制调用 | RenderBackend, Camera |
| **PhysicsSystem** | 物理模拟、碰撞检测 | PhysXManager |
| **PlayerSystem** | 角色控制、输入处理 | InputManager, PhysX |
| **InputManager** | 键盘鼠标输入 | Windows API |
| **PhysXManager** | PhysX初始化、角色控制器 | NVIDIA PhysX |
| **RenderBackend** | D3D11封装、GPU资源 | DirectX 11 |

---

## 核心系统详解

### 1. Engine (引擎主控)
**文件**: `src/core/Engine.h/cpp`

**职责**:
- 初始化所有子系统
- 运行主循环 (Update Loop)
- 管理帧率和时间步长
- 协调系统间通信

**关键方法**:
```cpp
bool Initialize(HWND hwnd, int width, int height);
void Run();        // 主循环入口
void Update();     // 每帧调用
void Shutdown();
```

**主循环伪代码**:
```cpp
while (running) {
    // 1. 处理Windows消息
    ProcessMessages();
    
    // 2. 计算帧时间
    float deltaTime = TimeManager::GetDeltaTime();
    
    // 3. 更新物理
    PhysXManager::Update(deltaTime);
    
    // 4. 更新玩家系统
    PlayerSystem::Update(deltaTime, registry);
    
    // 5. 渲染
    RenderSystem::Update(deltaTime, registry);
}
```

---

### 2. ECS (Entity Component System)
**库**: EnTT 3.16.0

**核心概念**:
- **Entity**: 唯一ID (uint32_t)，代表场景中的一个物体
- **Component**: 纯数据结构，定义实体属性
- **System**: 逻辑处理，遍历具有特定组件的实体

**示例 - 创建一个球体实体**:
```cpp
// 1. 创建实体
entt::entity sphereEntity = registry.create();

// 2. 添加组件
registry.emplace<TransformComponent>(sphereEntity, position, scale);
registry.emplace<MeshComponent>(sphereEntity, mesh, material);
registry.emplace<RenderPriorityComponent>(sphereEntity);

// 3. 系统自动处理
// RenderSystem会在Update时自动找到所有带MeshComponent的实体并渲染
```

---

### 3. SceneManager & SceneAssetLoader
**文件**: 
- `src/scene/SceneManager.h/cpp`
- `src/scene/SceneAssetLoader.h/cpp`

**职责**:
- **SceneManager**: 管理多个Scene，切换场景
- **SceneAssetLoader**: 加载3D模型、纹理、创建实体

**加载模型的三种方式**:

#### 方式1: 单个模型加载 (推荐用于测试)
```cpp
// 在main.cpp中直接调用
entt::entity sphere = SceneAssetLoader::LoadModelAsEntity(
    registry,
    scene,
    device,
    "assets/BlendObj/planet1.obj",           // OBJ模型路径
    "assets/Texture/stone_wall.jpg",         // 纹理路径
    DirectX::XMFLOAT3(3.0f, 2.0f, 0.0f),    // 位置
    DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f)     // 缩放
);
```

#### 方式2: 批量加载 (推荐用于大场景)
```cpp
// 创建场景配置文件: assets/scenes/main_scene.json
{
  "objects": [
    {
      "name": "planet1",
      "model": "assets/BlendObj/planet1.obj",
      "texture": "assets/Texture/planet_UV.png",
      "position": [0, 2, 0],
      "rotation": [0, 0, 0],
      "scale": [1, 1, 1]
    },
    {
      "name": "planet2",
      "model": "assets/BlendObj/planet1.obj",
      "texture": "assets/Texture/stone_wall.jpg",
      "position": [10, 2, 0],
      "scale": [2, 2, 2]
    }
    // ... 可以添加数百个物体
  ]
}

// 在main.cpp中加载整个场景
int loadedCount = SceneAssetLoader::LoadSceneFromFile(
    registry,
    scene,
    device,
    "assets/scenes/main_scene.json"
);
// 注意: 需要实现JSON解析器 (目前是TODO)
```

#### 方式3: 资源共享 - 实例化 (性能最优)
```cpp
// 适用于: 100个相同的石头、1000棵相同的树等

// 1. 只加载一次网格和材质
auto sharedMesh = SceneAssetLoader::LoadMeshResource("planet1.obj");
auto sharedMat = SceneAssetLoader::CreateMaterialResource(device, "stone.jpg");

// 2. 创建100个实例，共享相同的mesh/material
for (int i = 0; i < 100; i++) {
    entt::entity instance = registry.create();
    
    // 每个实例有独立的Transform (位置不同)
    auto& transform = registry.emplace<TransformComponent>(instance);
    transform.position = {i * 5.0f, 0, 0};
    
    // 但共享相同的Mesh和Material (节省内存!)
    registry.emplace<MeshComponent>(instance, sharedMesh, sharedMat);
    registry.emplace<RenderPriorityComponent>(instance);
}
```

**为什么不一个一个写到代码里?**
- ✅ **小场景** (< 10个物体): 可以在main.cpp中手写
- ❌ **大场景** (> 50个物体): 应该用JSON/XML配置文件
- ❌ **重复物体** (如森林、城市): 必须用资源共享实例化

---

### 4. RenderSystem (渲染系统)
**文件**: `src/graphics/RenderSystem.h/cpp`

**渲染流程**:
```
Update() 每帧调用
    ↓
1. FindActiveCamera()  ← 找到active camera
    ↓
2. UpdateRenderPriorities()  ← 计算所有实体到相机的距离
    ↓
3. 收集实体到渲染队列
    - 遍历 RenderableComponent 实体 → m_OpaqueQueue
    - 遍历 MeshComponent 实体 → m_OpaqueQueue / m_TransparentQueue
    ↓
4. 排序队列
    - Opaque: 前到后 (减少overdraw)
    - Transparent: 后到前 (正确混合)
    ↓
5. DrawQueue()  ← 依次绘制
    - 绑定Shader
    - 设置ConstantBuffer (世界矩阵)
    - 绑定VertexBuffer/IndexBuffer
    - 绑定Texture
    - DrawIndexed()
```

**支持的组件类型**:
- `RenderableComponent`: 旧系统，用于地面quad
- `MeshComponent`: 新系统，用于OBJ模型

---

### 5. PhysicsSystem & PhysXManager
**文件**: 
- `src/physics/PhysXManager.h/cpp`
- `src/physics/PhysicsSystem.h/cpp`

**职责**:
- **PhysXManager**: 初始化PhysX SDK，创建Scene
- **PhysicsSystem**: 更新物理模拟，处理碰撞

**角色控制器**:
```cpp
// 在main.cpp中创建
physx::PxCapsuleControllerDesc desc;
desc.height = 1.6f;  // 身高
desc.radius = 0.4f;  // 半径
desc.material = groundMaterial;  // 摩擦材质
physx::PxController* controller = controllerManager->createController(desc);

// 在PlayerSystem中移动
character.controller->move(displacement, minDist, deltaTime, filters);
```

---

### 6. PlayerSystem (玩家控制)
**文件**: `src/gameplay/PlayerSystem.h/cpp`

**Update流程**:
```cpp
Update(deltaTime, registry) {
    ProcessPlayerInput();      // 读取WASD/鼠标
    UpdatePlayerMovement();    // 移动角色控制器
    UpdatePlayerCamera();      // 更新相机朝向
}
```

**角色移动计算**:
```cpp
// 1. 获取相机朝向
float yaw = camera.yaw;
XMVECTOR forward = {sin(yaw), 0, cos(yaw)};
XMVECTOR right = {cos(yaw), 0, -sin(yaw)};

// 2. 计算移动方向 (相对于相机)
XMVECTOR moveDir = forward * forwardInput + right * rightInput;

// 3. 应用到PhysX
PxVec3 displacement(x * deltaTime, y * deltaTime, z * deltaTime);
controller->move(displacement, minDist, deltaTime, filters);
```

---

### 7. InputManager (输入管理)
**文件**: `src/input/InputManager.h/cpp`

**FPS鼠标控制**:
```cpp
UpdatePlayerInput() {
    // 1. 计算鼠标相对窗口中心的偏移
    int centerX = windowWidth / 2;
    float deltaX = mousePos.x - centerX;
    
    // 2. 存储到PlayerInput
    playerInput.lookInput.x = deltaX;
    
    // 3. 重置鼠标到中心 (实现无限旋转)
    SetCursorPos(centerX, centerY);
}
```

---

## 主循环流程

### 完整的一帧执行顺序

```cpp
// main.cpp
int main() {
    // ===== 初始化阶段 =====
    Engine::Initialize(hwnd, width, height);
        ├─ RenderBackend::Initialize()    // D3D11初始化
        ├─ PhysXManager::Initialize()     // PhysX初始化
        ├─ InputManager::Initialize()     // 输入初始化
        └─ SceneManager::CreateScene()    // 创建默认场景
    
    // 加载场景资源
    CreateGroundPlane();                   // 创建地面实体
    LoadSphereModel();                     // 加载球体实体
    CreatePlayerController();              // 创建玩家实体
    
    // ===== 主循环 =====
    Engine::Run();
        while (running) {
            // 1. 处理Windows消息
            PeekMessage(...);
            
            // 2. 计算deltaTime
            float deltaTime = currentTime - lastTime;
            
            // 3. 物理更新
            PhysXManager::Update(deltaTime);
                └─ scene->simulate(deltaTime);
                └─ scene->fetchResults();
            
            // 4. 玩家系统更新
            PlayerSystem::Update(deltaTime, registry);
                ├─ ProcessPlayerInput()
                │   └─ InputManager::Update()
                │       ├─ UpdateKeyboardState()
                │       ├─ UpdateMouseState()
                │       └─ UpdatePlayerInput()
                ├─ UpdatePlayerMovement()
                │   └─ controller->move(...)
                └─ UpdatePlayerCamera()
                    └─ camera.yaw += lookInput.x * sensitivity
            
            // 5. 渲染系统更新
            RenderSystem::Update(deltaTime, registry);
                ├─ FindActiveCamera()
                ├─ UpdateRenderPriorities()  // 计算距离
                ├─ 收集实体到队列
                ├─ 排序队列
                ├─ DrawQueue(opaqueQueue)
                └─ Present()
        }
    
    // ===== 清理阶段 =====
    Engine::Shutdown();
}
```

---

## 场景资源加载

### 推荐的项目结构

```
OuterWilds/
├── assets/
│   ├── models/          ← 所有3D模型
│   │   ├── planet1.obj
│   │   ├── player.obj
│   │   └── tree.obj
│   ├── textures/        ← 所有纹理
│   │   ├── stone_wall_diff.jpg
│   │   ├── grass.jpg
│   │   └── sky.jpg
│   └── scenes/          ← 场景配置文件
│       ├── level1.json
│       └── test_scene.json
├── src/
└── docs/
```

### 场景配置文件示例

**assets/scenes/level1.json**:
```json
{
  "name": "Level 1 - Solar System",
  "camera": {
    "position": [0, 5, -10],
    "fov": 75
  },
  "objects": [
    {
      "name": "Sun",
      "model": "assets/models/sphere.obj",
      "texture": "assets/textures/sun.jpg",
      "position": [0, 0, 0],
      "scale": [5, 5, 5],
      "tags": ["celestial", "light_source"]
    },
    {
      "name": "Earth",
      "model": "assets/models/sphere.obj",
      "texture": "assets/textures/earth.jpg",
      "position": [20, 0, 0],
      "scale": [2, 2, 2],
      "physics": {
        "type": "dynamic",
        "mass": 1.0
      }
    },
    {
      "name": "Moon",
      "model": "assets/models/sphere.obj",
      "texture": "assets/textures/moon.jpg",
      "position": [25, 0, 0],
      "scale": [0.5, 0.5, 0.5]
    }
  ],
  "lights": [
    {
      "type": "directional",
      "direction": [0, -1, 0],
      "color": [1, 1, 1],
      "intensity": 1.0
    }
  ]
}
```

### 加载大场景的最佳实践

```cpp
// 1. 创建资源管理器 (避免重复加载)
class ResourceManager {
    std::unordered_map<std::string, std::shared_ptr<Mesh>> meshCache;
    std::unordered_map<std::string, std::shared_ptr<Material>> materialCache;
    
public:
    std::shared_ptr<Mesh> GetMesh(const std::string& path) {
        if (meshCache.find(path) == meshCache.end()) {
            meshCache[path] = SceneAssetLoader::LoadMeshResource(path);
        }
        return meshCache[path];
    }
};

// 2. 异步加载 (避免卡顿)
std::thread loadThread([&]() {
    for (auto& objectDef : sceneConfig.objects) {
        auto mesh = resourceManager.GetMesh(objectDef.modelPath);
        auto material = resourceManager.GetMaterial(objectDef.texturePath);
        
        // 主线程创建实体
        mainThreadQueue.push([=]() {
            CreateEntity(mesh, material, objectDef.position);
        });
    }
});

// 3. 空间分区 (视锥剔除)
// 只渲染相机视野内的物体
OctreeNode* rootNode;
rootNode->Query(cameraFrustum, visibleEntities);
```

---

## 关键变量索引

### Transform 相关
| 变量 | 类型 | 含义 | 范围 |
|------|------|------|------|
| `position` | XMFLOAT3 | 世界坐标 | (-∞, ∞) |
| `rotation` | XMFLOAT4 | 四元数旋转 | 单位四元数 |
| `scale` | XMFLOAT3 | 缩放 | (0, ∞) |

### Render Priority 相关
| 变量 | 类型 | 含义 | 典型值 |
|------|------|------|--------|
| `sortKey` | uint32_t | 排序键 | 0-65535 |
| `renderPass` | int | 渲染通道 | 0=Opaque, 1=Transparent |
| `distanceToCamera` | float | 到相机距离 | 自动计算 |
| `lodLevel` | int | LOD级别 | 0=最高, 1=中, 2=低 |
| `visible` | bool | 是否可见 | true/false |
| `castShadow` | bool | 投射阴影 | true/false |
| `receiveShadow` | bool | 接收阴影 | true/false |

### Camera 相关
| 变量 | 类型 | 含义 | 典型值 |
|------|------|------|--------|
| `position` | XMFLOAT3 | 相机位置 | 角色位置+眼高 |
| `target` | XMFLOAT3 | 看向位置 | 根据yaw/pitch计算 |
| `yaw` | float | 水平旋转角 | 0-2π (弧度) |
| `pitch` | float | 俯仰角 | -π/2 ~ π/2 |
| `fov` | float | 视野角度 | 60-90 (度) |
| `aspectRatio` | float | 宽高比 | 16/9 = 1.778 |
| `nearPlane` | float | 近裁剪面 | 0.1 |
| `farPlane` | float | 远裁剪面 | 1000.0 |
| `lookSensitivity` | float | 鼠标灵敏度 | 0.001-0.01 |

### Character Controller 相关
| 变量 | 类型 | 含义 | 典型值 |
|------|------|------|--------|
| `moveSpeed` | float | 移动速度 | 5.0 m/s |
| `jumpSpeed` | float | 跳跃初速度 | 8.0 m/s |
| `gravity` | float | 重力加速度 | -20.0 m/s² |
| `verticalVelocity` | float | 当前垂直速度 | 动态计算 |
| `isGrounded` | bool | 是否在地面 | 碰撞检测 |
| `forwardInput` | float | 前后输入 | -1 ~ 1 (W/S) |
| `rightInput` | float | 左右输入 | -1 ~ 1 (A/D) |

### PhysX 相关
| 变量 | 类型 | 含义 | 典型值 |
|------|------|------|--------|
| `controller->height` | float | 胶囊高度 | 1.6m |
| `controller->radius` | float | 胶囊半径 | 0.4m |
| `stepOffset` | float | 可爬台阶高度 | 0.5m |
| `slopeLimit` | float | 最大坡度 | cos(45°) = 0.707 |
| `minDist` | float | 最小移动距离 | 0.0001 (更流畅) |
| `material->staticFriction` | float | 静摩擦系数 | 0.8 |
| `material->dynamicFriction` | float | 动摩擦系数 | 0.6 |
| `material->restitution` | float | 弹性系数 | 0.1 |

### Input 相关
| 变量 | 类型 | 含义 |
|------|------|------|
| `moveInput` | XMFLOAT2 | WASD输入 (x=左右, y=前后) |
| `lookInput` | XMFLOAT2 | 鼠标偏移 (x=水平, y=垂直) |
| `mouseLookEnabled` | bool | 鼠标控制启用 |
| `jumpPressed` | bool | 空格键按下 |
| `sprintHeld` | bool | Shift键按住 |

### Mesh & Material 相关
| 变量 | 类型 | 含义 |
|------|------|------|
| `vertexBuffer` | ID3D11Buffer* | GPU顶点缓冲 |
| `indexBuffer` | ID3D11Buffer* | GPU索引缓冲 |
| `albedoTexture` | string | 漫反射贴图路径 |
| `normalTexture` | string | 法线贴图路径 |
| `shaderProgram` | void* | 临时存储纹理SRV |
| `metallic` | float | 金属度 (PBR) 0-1 |
| `roughness` | float | 粗糙度 (PBR) 0-1 |

---

## 调试指南

### 1. 查看实体数量
```cpp
// 在任何System的Update中
size_t entityCount = registry.size();
DebugManager::Log("Debug", "Total entities: " + std::to_string(entityCount));
```

### 2. 检查特定组件的实体
```cpp
auto view = registry.view<MeshComponent>();
DebugManager::Log("Debug", "Entities with MeshComponent: " + std::to_string(view.size()));
```

### 3. 调试渲染队列
```cpp
// RenderSystem.cpp 已有
// 每60帧输出一次渲染顺序
[RenderSystem] Opaque Queue:
  [0] Entity ID=0, sortKey=1000, distance=5.080354
  [1] Entity ID=1, sortKey=1000, distance=11.503248
```

### 4. 调试角色移动
```cpp
// PlayerSystem.cpp UpdatePlayerMovement()
DebugManager::Log("Player", 
    "Pos: (" + std::to_string(pos.x) + ", " + 
               std::to_string(pos.y) + ", " + 
               std::to_string(pos.z) + ")");
DebugManager::Log("Player", "Grounded: " + std::to_string(isGrounded));
```

### 5. 性能分析
```cpp
// 在Update开头
auto startTime = std::chrono::high_resolution_clock::now();

// 在Update结尾
auto endTime = std::chrono::high_resolution_clock::now();
float ms = std::chrono::duration<float, std::milli>(endTime - startTime).count();
DebugManager::Log("Perf", "RenderSystem: " + std::to_string(ms) + "ms");
```

### 6. 常见问题排查

| 问题 | 可能原因 | 检查 |
|------|----------|------|
| 物体不显示 | 没有MeshComponent/RenderableComponent | registry.view<MeshComponent>().size() |
| 物体不显示 | visible=false | priority.visible |
| 物体不显示 | 不在相机视野内 | distanceToCamera |
| 物体不显示 | 没有GPU缓冲 | mesh->vertexBuffer != nullptr |
| 纹理不显示 | 纹理路径错误 | TextureLoader加载失败 |
| 移动卡顿 | minDist太大 | 改为0.0001 |
| 鼠标失控 | 没有重置到中心 | SetCursorPos每帧调用 |
| 掉出地面 | 没有碰撞体 | PxRigidStatic创建 |

---

## 总结

### ECS设计优势
✅ **数据驱动**: 所有物体都是Entity+Components  
✅ **易扩展**: 新增Component不影响现有代码  
✅ **性能优化**: 缓存友好的遍历方式  
✅ **解耦**: System独立处理各自逻辑  

### 加载大场景的正确姿势
1. **小场景** (测试): 直接在main.cpp调用LoadModelAsEntity
2. **中等场景** (关卡): 使用JSON配置文件 + LoadSceneFromFile
3. **大场景** (开放世界): 资源共享实例化 + 空间分区 + 异步加载

### 关键设计模式
- **Singleton**: Engine, InputManager, PhysXManager
- **Component Pattern**: ECS架构核心
- **Resource Manager**: 共享Mesh和Material
- **Command Pattern**: PlayerInput → CharacterController

---

## 下一步开发建议

1. **实现JSON场景加载器** (优先级: 高)
   - 使用 nlohmann/json 库
   - 实现 SceneAssetLoader::LoadSceneFromFile

2. **添加资源管理器** (优先级: 高)
   - 避免重复加载相同模型/纹理
   - 引用计数管理生命周期

3. **实现视锥剔除** (优先级: 中)
   - 只渲染相机视野内的物体
   - 提升大场景性能

4. **添加LOD系统** (优先级: 中)
   - 根据距离切换模型细节
   - 减少顶点数量

5. **异步资源加载** (优先级: 低)
   - 后台线程加载模型
   - 避免主线程卡顿

---

**文档版本**: v1.0  
**最后更新**: 2025年12月3日  
**作者**: OuterWilds ECS Team
