# OuterWilds ECS Engine

基于ECS架构的3D游戏引擎,使用PhysX物理引擎和DirectX 11渲染。

## 特性

- ✅ ECS架构 (EnTT 3.16.0)
- ✅ PhysX 5.5.0 物理引擎
- ✅ DirectX 11 渲染
- ✅ 第一人称角色控制器
- ✅ OBJ模型加载
- ✅ 纹理支持 (JPG/PNG)
- ✅ 场景管理系统

## 环境要求

- Windows 10/11
- Visual Studio 2022
- CMake 3.15+
- vcpkg

## 快速开始

### 1. 克隆仓库

```bash
git clone https://github.com/nostune/CG.git
cd CG
```

### 2. 安装vcpkg

```powershell
# 克隆vcpkg到项目目录
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
```

### 3. 安装依赖

```powershell
# 安装EnTT
.\vcpkg install entt:x64-windows

# 安装PhysX (需要较长时间,约10-20分钟)
.\vcpkg install physx:x64-windows
```

### 4. 配置和编译

```powershell
# 返回项目根目录
cd ..

# 配置CMake
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=.\vcpkg\scripts\buildsystems\vcpkg.cmake -A x64

# 编译Debug版本
cmake --build build --config Debug

# 运行
.\build\Debug\OuterWildsECS.exe
```

## 项目结构

```
OuterWilds/
├── src/                    # 源代码
│   ├── core/              # 引擎核心 (Engine, ECS, TimeManager)
│   ├── graphics/          # 渲染系统 (RenderSystem, RenderBackend)
│   ├── physics/           # 物理系统 (PhysXManager, PhysicsSystem)
│   ├── gameplay/          # 游戏逻辑 (PlayerSystem)
│   ├── input/             # 输入管理 (InputManager)
│   └── scene/             # 场景管理 (SceneManager, SceneAssetLoader)
├── shaders/               # HLSL着色器
│   └── basic.hlsl        # 基础着色器
├── docs/                  # 文档
│   └── ENGINE_ARCHITECTURE.md  # 引擎架构详解
├── CMakeLists.txt         # CMake配置
└── README.md             # 本文件
```

## 使用说明

### 控制

- **WASD**: 移动
- **鼠标**: 视角控制 (360度无限旋转)
- **空格**: 跳跃
- **Shift**: 冲刺 (TODO)
- **ESC + Backspace**: 切换鼠标锁定

### 加载3D模型

参考 `docs/ENGINE_ARCHITECTURE.md` 了解详细的场景加载方法。

**方式1: 单个模型加载**
```cpp
entt::entity sphere = SceneAssetLoader::LoadModelAsEntity(
    registry,
    scene,
    device,
    "assets/BlendObj/planet1.obj",
    "assets/Texture/stone_wall.jpg",
    DirectX::XMFLOAT3(3.0f, 2.0f, 0.0f),  // 位置
    DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f)   // 缩放
);
```

**方式2: JSON场景加载** (推荐大场景)
```json
{
  "objects": [
    {
      "model": "assets/BlendObj/planet1.obj",
      "texture": "assets/Texture/stone_wall.jpg",
      "position": [0, 2, 0],
      "scale": [1, 1, 1]
    }
  ]
}
```

**方式3: 资源共享实例化** (性能最优)
```cpp
// 加载一次,创建100个实例
auto sharedMesh = SceneAssetLoader::LoadMeshResource("planet1.obj");
auto sharedMat = SceneAssetLoader::CreateMaterialResource(device, "stone.jpg");

for (int i = 0; i < 100; i++) {
    auto entity = registry.create();
    registry.emplace<TransformComponent>(entity, position);
    registry.emplace<MeshComponent>(entity, sharedMesh, sharedMat);
}
```

## VS Code 任务

项目包含`.vscode/tasks.json`,提供以下任务:

- **CMake Configure**: 配置项目
- **Build Debug**: 编译Debug版本
- **Build Release**: 编译Release版本
- **Clean Build**: 清理构建
- **Run Debug**: 运行Debug版本
- **Build and Run**: 编译并运行

使用 `Ctrl+Shift+B` 快速访问构建任务。

## 架构文档

详细的引擎架构说明请查看: **[ENGINE_ARCHITECTURE.md](docs/ENGINE_ARCHITECTURE.md)**

包含内容:
- 🔧 系统详解和职责划分
- 🔄 主循环执行流程
- 📦 场景资源加载方法 (单个/批量/实例化)
- 🔑 关键变量索引 (priority, sortKey等)
- 🐛 调试指南和常见问题

## 核心系统

| 系统 | 职责 |
|------|------|
| **Engine** | 主循环、系统协调 |
| **SceneManager** | 场景管理、实体创建 |
| **RenderSystem** | 渲染队列、绘制调用 |
| **PhysicsSystem** | 物理模拟、碰撞检测 |
| **PlayerSystem** | 角色控制、输入处理 |
| **InputManager** | 键盘鼠标输入 |

## 开发路线

- [x] 基础ECS框架
- [x] PhysX集成
- [x] 角色控制器 (胶囊碰撞体)
- [x] FPS鼠标控制 (中心重置模式)
- [x] OBJ模型加载
- [x] 纹理支持 (WIC加载器)
- [x] 场景资源加载器
- [x] 渲染队列和排序
- [ ] JSON场景加载器
- [ ] 资源管理器 (缓存/引用计数)
- [ ] 视锥剔除
- [ ] LOD系统
- [ ] PBR材质系统
- [ ] 阴影映射
- [ ] 后处理效果

## 技术栈

| 技术 | 版本 |
|------|------|
| **ECS** | EnTT 3.16.0 |
| **物理** | PhysX 5.5.0 |
| **渲染** | DirectX 11 |
| **构建** | CMake 3.15+ |
| **包管理** | vcpkg |
| **语言** | C++17 |
| **IDE** | Visual Studio 2022 |

## 性能优化

- ✅ 渲染队列排序 (前到后减少overdraw)
- ✅ 组件缓存友好遍历 (EnTT)
- ✅ 资源共享实例化支持
- 🔄 视锥剔除 (TODO)
- 🔄 空间分区 Octree (TODO)
- 🔄 多线程资源加载 (TODO)

## 常见问题

### Q: 编译失败,找不到PhysX?
**A**: 确保vcpkg正确安装了PhysX: `.\vcpkg list | Select-String physx`

### Q: 程序启动后全白/全黑屏幕?
**A**: 检查相机位置和朝向,确保在场景内。参考文档调整相机参数。

### Q: 鼠标控制不正常?
**A**: 确保`mouseLookEnabled=true`,并且每帧调用`SetCursorPos`重置到中心。

### Q: 模型不显示?
**A**: 检查以下几点:
1. 是否添加了`MeshComponent`和`RenderPriorityComponent`
2. `visible=true`
3. 模型和纹理路径正确
4. GPU缓冲是否创建成功

### Q: 如何加载大场景?
**A**: 参考`docs/ENGINE_ARCHITECTURE.md`的"场景资源加载"章节,使用JSON配置文件批量加载。

## 贡献

欢迎提交Issue和Pull Request!

## 许可证

MIT License

## 作者

- **nostune** - [GitHub](https://github.com/nostune)

## 致谢

- [EnTT](https://github.com/skypjack/entt) - 高性能ECS库
- [NVIDIA PhysX](https://github.com/NVIDIA-Omniverse/PhysX) - 物理引擎
- [Microsoft DirectX](https://docs.microsoft.com/en-us/windows/win32/directx) - 图形API
- [vcpkg](https://github.com/microsoft/vcpkg) - C++包管理器
