# 模型加载与物理设置指南

## 📌 核心问题解答

### Q1: 模型的物理属性（刚体/碰撞体）在哪里设置？
**答：必须在代码里设置，Blender无法导出物理信息到OBJ文件。**

| 属性类型 | Blender建模时 | OBJ导出时 | 代码运行时 |
|---------|--------------|----------|-----------|
| **几何形状** | ✅ 必须设置 | ✅ 导出到.obj | ❌ 只读取 |
| **UV坐标** | ✅ 必须设置 | ✅ 导出到.obj | ❌ 只读取 |
| **材质/纹理** | ✅ 可选设置 | ✅ 导出到.mtl | ⚠️ 需要解析 |
| **物理属性** | ❌ 不支持 | ❌ OBJ不支持 | ✅ **必须在代码实现** |
| **质量/摩擦力** | ❌ 不支持 | ❌ OBJ不支持 | ✅ **必须在代码实现** |

---

### Q2: 材质贴图在哪里设置？
**答：材质路径在Blender设置，导出到MTL文件，代码需要解析MTL。**

#### 完整流程：

1. **Blender中设置材质**
   ```
   Shading工作区 → 添加Image Texture节点 → 加载diffuse贴图
   → 连接到Principled BSDF的Base Color
   ```

2. **导出OBJ时选项**
   ```
   ✅ Include UVs
   ✅ Write Materials (.mtl)
   ✅ Path Mode: Relative
   ```

3. **MTL文件示例**
   ```mtl
   newmtl plastered_stone_wall
   map_Kd textures/plastered_stone_wall_diff_2k.jpg  # 漫反射贴图路径
   map_Pr textures/plastered_stone_wall_rough_2k.exr # 粗糙度贴图
   map_Bump textures/plastered_stone_wall_nor_gl_2k.exr # 法线贴图
   ```

4. **代码自动解析MTL**
   ```cpp
   // 方式1：自动从MTL解析（推荐）
   SceneAssetLoader::LoadModelAsEntity(
       registry, scene, device,
       "assets/BlendObj/planet1.obj",
       "",  // 空字符串 = 自动解析MTL
       position, scale
   );
   
   // 方式2：手动指定纹理路径（覆盖MTL）
   SceneAssetLoader::LoadModelAsEntity(
       registry, scene, device,
       "assets/BlendObj/planet1.obj",
       "assets/Texture/custom_texture.jpg",  // 显式指定
       position, scale
   );
   ```

---

## 🛠️ 代码实现指南

### 1. 加载模型（仅渲染，无物理）

```cpp
entt::entity sphere = SceneAssetLoader::LoadModelAsEntity(
    registry, scene, device,
    "assets/BlendObj/planet1.obj",
    "",  // 自动从MTL加载纹理
    DirectX::XMFLOAT3(3.0f, 2.0f, 0.0f),  // 位置
    DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f)   // 缩放
);
```

**效果**：
- ✅ 可以看到模型和纹理
- ❌ 可以穿透模型（无碰撞）
- ❌ 不受重力影响

---

### 2. 加载模型 + 静态碰撞体（不可移动）

```cpp
PhysicsOptions staticPhysics;
staticPhysics.addCollider = true;
staticPhysics.addRigidBody = true;
staticPhysics.shape = PhysicsOptions::ColliderShape::Sphere;
staticPhysics.sphereRadius = 1.0f;
staticPhysics.mass = 0.0f;  // 质量为0 = 静态物体
staticPhysics.isKinematic = true;

entt::entity staticSphere = SceneAssetLoader::LoadModelAsEntity(
    registry, scene, device,
    "assets/BlendObj/planet1.obj",
    "", position, scale,
    &staticPhysics  // 传入物理配置
);
```

**效果**：
- ✅ 可以看到模型和纹理
- ✅ 玩家会被阻挡（有碰撞）
- ❌ 物体本身不会移动或掉落

---

### 3. 加载模型 + 动态刚体（可以掉落/碰撞）

```cpp
PhysicsOptions dynamicPhysics;
dynamicPhysics.addCollider = true;
dynamicPhysics.addRigidBody = true;
dynamicPhysics.shape = PhysicsOptions::ColliderShape::Sphere;
dynamicPhysics.sphereRadius = 1.0f;
dynamicPhysics.mass = 10.0f;  // 10kg
dynamicPhysics.useGravity = true;
dynamicPhysics.isKinematic = false;  // 动态物体
dynamicPhysics.staticFriction = 0.6f;
dynamicPhysics.dynamicFriction = 0.5f;
dynamicPhysics.restitution = 0.3f;  // 弹性系数

entt::entity dynamicSphere = SceneAssetLoader::LoadModelAsEntity(
    registry, scene, device,
    "assets/BlendObj/planet1.obj",
    "", 
    DirectX::XMFLOAT3(3.0f, 5.0f, 0.0f),  // 高处掉落
    DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f),
    &dynamicPhysics
);
```

**效果**：
- ✅ 可以看到模型和纹理
- ✅ 受重力影响会掉落
- ✅ 会和地面/其他物体碰撞
- ✅ 玩家可以推动它

---

### 4. 加载模型 + 盒状碰撞体

```cpp
PhysicsOptions boxPhysics;
boxPhysics.addCollider = true;
boxPhysics.addRigidBody = true;
boxPhysics.shape = PhysicsOptions::ColliderShape::Box;
boxPhysics.boxExtent = DirectX::XMFLOAT3(2.0f, 2.0f, 2.0f);  // 盒子尺寸
boxPhysics.mass = 5.0f;
boxPhysics.useGravity = true;
boxPhysics.isKinematic = false;

entt::entity box = SceneAssetLoader::LoadModelAsEntity(
    registry, scene, device,
    "assets/BlendObj/cube.obj",
    "", position, scale,
    &boxPhysics
);
```

---

## 📊 PhysicsOptions 参数详解

| 参数 | 类型 | 默认值 | 说明 |
|-----|------|--------|-----|
| `addCollider` | bool | false | 是否添加碰撞体 |
| `addRigidBody` | bool | false | 是否添加刚体组件 |
| `shape` | enum | Sphere | 碰撞体形状：Box/Sphere/ConvexMesh |
| `sphereRadius` | float | 1.0f | 球体碰撞体半径 |
| `boxExtent` | XMFLOAT3 | (1,1,1) | 盒状碰撞体尺寸 |
| `mass` | float | 1.0f | 质量（0=静态物体） |
| `useGravity` | bool | true | 是否受重力影响 |
| `isKinematic` | bool | false | 是否为运动学物体（代码控制） |
| `staticFriction` | float | 0.5f | 静摩擦系数 |
| `dynamicFriction` | float | 0.5f | 动摩擦系数 |
| `restitution` | float | 0.3f | 弹性系数（0=不弹，1=完全弹） |

---

## 🎨 Blender工作流程

### 步骤1：建模和UV展开
1. 创建模型（Modeling工作区）
2. 选择模型 → Tab进入编辑模式
3. 按U键 → Smart UV Project（自动UV展开）
4. 在UV Editor中调整UV布局

### 步骤2：材质设置
1. 切换到Shading工作区
2. 添加节点：
   - Image Texture节点（加载漫反射贴图）
   - 连接到Principled BSDF的Base Color
3. 可选：添加Normal Map、Roughness Map等

### 步骤3：导出OBJ
```
File → Export → Wavefront (.obj)

导出选项：
✅ Include: Selected Objects
✅ Include UVs
✅ Write Materials
✅ Path Mode: Relative  # 重要！使用相对路径
✅ Triangulate Faces
```

### 步骤4：检查文件结构
```
assets/
├── BlendObj/
│   ├── planet1.obj      # 几何数据
│   ├── planet1.mtl      # 材质定义
│   └── textures/        # 纹理文件夹
│       ├── diffuse.jpg
│       ├── normal.exr
│       └── roughness.exr
```

---

## 🚀 批量加载场景（未来实现）

### JSON场景文件示例
```json
{
  "scene_name": "MainLevel",
  "objects": [
    {
      "name": "rock1",
      "model": "assets/BlendObj/rock.obj",
      "position": [10, 0, 5],
      "scale": [1, 1, 1],
      "physics": {
        "collider": "Sphere",
        "radius": 2.0,
        "mass": 50.0,
        "friction": 0.7
      }
    },
    {
      "name": "tree1",
      "model": "assets/BlendObj/tree.obj",
      "position": [5, 0, -3],
      "scale": [2, 2, 2],
      "physics": {
        "collider": "Box",
        "extent": [1, 5, 1],
        "mass": 0.0,
        "static": true
      }
    }
  ]
}
```

### 代码调用
```cpp
int loadedCount = SceneAssetLoader::LoadSceneFromFile(
    registry, scene, device,
    "assets/Scenes/main_level.json"
);
// 返回加载的实体数量
```

---

## ⚠️ 常见问题

### 1. 材质显示为灰色网格？
**原因**：
- MTL文件中的纹理路径不正确
- 纹理文件不存在
- 纹理格式不支持

**解决方案**：
```cpp
// 检查DebugManager日志
DebugManager::GetInstance().Log("SceneAssetLoader", "Found map_Kd in MTL: ...");
DebugManager::GetInstance().Log("SceneAssetLoader", "Loaded texture: ...");

// 如果MTL解析失败，手动指定纹理路径
SceneAssetLoader::LoadModelAsEntity(
    registry, scene, device,
    "assets/BlendObj/planet1.obj",
    "assets/Texture/plastered_stone_wall_diff_2k.jpg",  // 绝对路径
    position, scale
);
```

### 2. 模型可以穿透？
**原因**：没有添加物理组件

**解决方案**：
```cpp
PhysicsOptions physics;
physics.addCollider = true;
physics.addRigidBody = true;
physics.mass = 10.0f;  // 动态物体

SceneAssetLoader::LoadModelAsEntity(
    registry, scene, device,
    objPath, texturePath, position, scale,
    &physics  // 不要忘记传入这个参数！
);
```

### 3. 物体一直在抖动？
**原因**：
- 碰撞体嵌入地面
- 质量太小
- 摩擦力太低

**解决方案**：
```cpp
physics.mass = 10.0f;  // 增加质量
physics.staticFriction = 0.6f;  // 增加摩擦力
physics.dynamicFriction = 0.5f;

// 确保物体初始位置在地面之上
DirectX::XMFLOAT3 position(0, 2.0f, 0);  // y=2而不是y=0
```

---

## 📝 总结

| 设置项 | Blender | OBJ/MTL | 代码 |
|-------|---------|---------|-----|
| **模型顶点/面** | ✅ 建模 | ✅ 导出 | ❌ 只读取 |
| **UV坐标** | ✅ UV展开 | ✅ 导出 | ❌ 只读取 |
| **纹理路径** | ✅ 材质节点 | ✅ MTL文件 | ✅ 解析MTL |
| **碰撞体形状** | ❌ | ❌ | ✅ PhysicsOptions.shape |
| **质量/重力** | ❌ | ❌ | ✅ PhysicsOptions.mass |
| **摩擦力** | ❌ | ❌ | ✅ PhysicsOptions.friction |

**关键原则**：
- **几何和UV**：在Blender完成
- **材质贴图**：Blender设置路径，代码解析MTL
- **物理属性**：完全在代码中配置（OBJ格式不支持）
