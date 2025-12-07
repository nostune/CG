# 星球重力系统设计文档

## 🎯 目标

实现《Outer Wilds》风格的星球重力系统，允许玩家在任意球面上行走，支持多个独立星球。

---

## 🏗️ 系统架构

### 核心组件位置

```
src/
├── physics/                    # ✅ 重力系统主要实现位置
│   ├── GravitySystem.h         # 重力计算系统
│   ├── GravitySystem.cpp
│   ├── components/
│   │   ├── GravitySourceComponent.h   # 星球重力源
│   │   └── GravityAffectedComponent.h # 受重力影响的对象
│   └── PhysicsSystem.cpp       # 集成到物理更新
│
└── gameplay/                   # ✅ 玩家对齐和控制
    ├── PlayerAlignmentSystem.h # 玩家姿态对齐系统
    ├── PlayerAlignmentSystem.cpp
    └── FPSCharacterController.h # 修改控制逻辑
```

### 设计原则

1. **物理层（Physics）**：负责重力计算、力的施加
2. **游戏层（Gameplay）**：负责玩家姿态对齐、相机旋转
3. **解耦设计**：重力源和受影响对象通过组件通信
4. **多星球支持**：场景中可以有多个重力源

---

## 📐 数学原理

### 1. 球面重力方向

```cpp
// 重力方向 = 从玩家位置指向星球中心
Vector3 playerPos = player.position;
Vector3 planetCenter = planet.position;
Vector3 gravityDir = normalize(planetCenter - playerPos);

// 重力加速度（与距离平方成反比，可选）
float distance = length(planetCenter - playerPos);
float radius = planet.radius;

// 方式1: 恒定重力（简单）
float gravityStrength = 9.8f;

// 方式2: 真实重力（距离衰减）
float gravityStrength = G * planet.mass / (distance * distance);

// 方式3: 表面恒定+距离衰减（推荐）
if (distance < radius * 2) {
    gravityStrength = 9.8f;  // 靠近表面时恒定
} else {
    gravityStrength = 9.8f * (radius / distance)^2;  // 远离时衰减
}
```

### 2. 玩家姿态对齐

```cpp
// 玩家的"上方向"应该始终指向远离星球中心
Vector3 desiredUp = -gravityDir;  // 与重力方向相反

// 当前玩家的上方向
Vector3 currentUp = player.transform.up;

// 平滑旋转到目标方向（使用四元数）
Quaternion targetRotation = LookRotation(player.forward, desiredUp);
player.rotation = Slerp(player.rotation, targetRotation, alignSpeed * deltaTime);
```

### 3. 相机旋转

```cpp
// 相机始终跟随玩家的局部坐标系
// Pitch: 上下看（相对玩家的局部X轴）
// Yaw: 左右看（相对玩家的局部Y轴）

// 鼠标输入
yaw += mouseDeltaX * sensitivity;
pitch -= mouseDeltaY * sensitivity;
pitch = clamp(pitch, -89, 89);

// 应用旋转（局部空间）
Quaternion yawRotation = AngleAxis(yaw, player.up);
Quaternion pitchRotation = AngleAxis(pitch, player.right);
camera.rotation = player.rotation * yawRotation * pitchRotation;
```

### 4. 移动控制

```cpp
// WASD移动始终基于玩家的局部坐标系
Vector3 moveDir = Vector3::Zero;

if (Input.W) moveDir += player.forward;   // 相对玩家前方
if (Input.S) moveDir -= player.forward;
if (Input.A) moveDir -= player.right;     // 相对玩家右方
if (Input.D) moveDir += player.right;

moveDir = normalize(moveDir);

// 移动速度
Vector3 velocity = moveDir * moveSpeed;

// 应用到角色控制器（PhysX）
characterController->move(velocity * deltaTime);
```

---

## 🔧 实现步骤

### Phase 1: 核心重力组件（Physics层）

#### 1.1 GravitySourceComponent
```cpp
struct GravitySourceComponent {
    float mass = 1000.0f;           // 星球质量
    float radius = 10.0f;           // 星球半径
    float surfaceGravity = 9.8f;    // 表面重力加速度
    bool useRealistic = false;       // 是否使用真实物理衰减
    
    // 可选：大气层厚度（用于软过渡）
    float atmosphereHeight = 5.0f;
};
```

#### 1.2 GravityAffectedComponent
```cpp
struct GravityAffectedComponent {
    bool affectedByGravity = true;
    float gravityScale = 1.0f;       // 重力缩放（0=无重力，2=双倍重力）
    
    // 运行时状态
    entt::entity currentGravitySource = entt::null;  // 当前影响的星球
    DirectX::XMFLOAT3 currentGravityDir = {0, -1, 0};
    float currentGravityStrength = 9.8f;
};
```

#### 1.3 GravitySystem
```cpp
class GravitySystem : public System {
public:
    void Update(float deltaTime, entt::registry& registry) override {
        // 1. 遍历所有受重力影响的对象
        auto affectedView = registry.view<GravityAffectedComponent, TransformComponent>();
        
        for (auto entity : affectedView) {
            auto& affected = affectedView.get<GravityAffectedComponent>(entity);
            auto& transform = affectedView.get<TransformComponent>(entity);
            
            // 2. 找到最近的重力源
            entt::entity nearestSource = FindNearestGravitySource(registry, transform.position);
            
            if (nearestSource != entt::null) {
                // 3. 计算重力方向和强度
                CalculateGravity(registry, entity, nearestSource, affected, transform);
                
                // 4. 应用重力到物理系统
                ApplyGravityForce(registry, entity, affected);
            }
        }
    }
    
private:
    entt::entity FindNearestGravitySource(entt::registry& registry, const XMFLOAT3& position);
    void CalculateGravity(entt::registry& registry, entt::entity entity, entt::entity source, ...);
    void ApplyGravityForce(entt::registry& registry, entt::entity entity, ...);
};
```

---

### Phase 2: 玩家对齐系统（Gameplay层）

#### 2.1 PlayerAlignmentComponent
```cpp
struct PlayerAlignmentComponent {
    float alignmentSpeed = 5.0f;     // 对齐速度（越大越快）
    bool autoAlign = true;           // 自动对齐到重力方向
    
    // 约束
    bool lockRoll = true;            // 锁定翻滚（防止玩家侧翻）
};
```

#### 2.2 PlayerAlignmentSystem
```cpp
class PlayerAlignmentSystem : public System {
public:
    void Update(float deltaTime, entt::registry& registry) override {
        // 遍历所有玩家
        auto playerView = registry.view<
            PlayerComponent,
            PlayerAlignmentComponent,
            GravityAffectedComponent,
            TransformComponent
        >();
        
        for (auto entity : playerView) {
            auto& alignment = playerView.get<PlayerAlignmentComponent>(entity);
            auto& gravity = playerView.get<GravityAffectedComponent>(entity);
            auto& transform = playerView.get<TransformComponent>(entity);
            
            if (alignment.autoAlign) {
                // 计算目标上方向（与重力相反）
                XMVECTOR desiredUp = XMVector3Normalize(
                    XMLoadFloat3(&gravity.currentGravityDir) * -1.0f
                );
                
                // 获取当前前方向（保持玩家朝向）
                XMVECTOR currentForward = GetForwardFromRotation(transform.rotation);
                
                // 重新计算对齐的旋转（保持前方向，调整上方向）
                XMVECTOR targetRight = XMVector3Normalize(XMVector3Cross(desiredUp, currentForward));
                XMVECTOR targetForward = XMVector3Cross(targetRight, desiredUp);
                
                // 构造目标四元数
                XMMATRIX targetMatrix = ConstructRotationMatrix(targetForward, desiredUp, targetRight);
                XMVECTOR targetRotation = XMQuaternionRotationMatrix(targetMatrix);
                
                // 平滑插值
                XMVECTOR currentRotation = XMLoadFloat4(&transform.rotation);
                XMVECTOR newRotation = XMQuaternionSlerp(
                    currentRotation,
                    targetRotation,
                    alignment.alignmentSpeed * deltaTime
                );
                
                XMStoreFloat4(&transform.rotation, newRotation);
            }
        }
    }
};
```

---

### Phase 3: 修改FPS控制器

#### 3.1 更新FPSCharacterController

```cpp
// 旧版本：世界空间的WASD移动
Vector3 moveDir = {0, 0, 0};
if (input.forward) moveDir.z += 1;  // ❌ 固定的世界Z轴

// 新版本：玩家局部空间的WASD移动
XMVECTOR forward = GetPlayerForward(transform);   // 玩家前方
XMVECTOR right = GetPlayerRight(transform);       // 玩家右方
XMVECTOR up = GetPlayerUp(transform);             // 玩家上方

XMVECTOR moveDir = XMVectorZero();
if (input.forward) moveDir += forward;  // ✅ 相对玩家前方
if (input.back)    moveDir -= forward;
if (input.left)    moveDir -= right;
if (input.right)   moveDir += right;

moveDir = XMVector3Normalize(moveDir);
```

#### 3.2 更新相机旋转

```cpp
// 旧版本：固定世界Y轴旋转
yaw += mouseDeltaX;
camera.rotation = QuaternionFromEuler(pitch, yaw, 0);  // ❌ 固定轴

// 新版本：相对玩家局部轴旋转
yaw += mouseDeltaX * sensitivity;
pitch -= mouseDeltaY * sensitivity;
pitch = clamp(pitch, -89.0f, 89.0f);

// 相机跟随玩家旋转
XMVECTOR playerRotation = XMLoadFloat4(&transform.rotation);

// Yaw在玩家的局部Y轴（up）上旋转
XMVECTOR yawRotation = XMQuaternionRotationAxis(GetPlayerUp(transform), XMConvertToRadians(yaw));

// Pitch在玩家的局部X轴（right）上旋转
XMVECTOR pitchRotation = XMQuaternionRotationAxis(GetPlayerRight(transform), XMConvertToRadians(pitch));

// 组合旋转
XMVECTOR cameraRotation = XMQuaternionMultiply(playerRotation, XMQuaternionMultiply(yawRotation, pitchRotation));
```

---

## 🎮 完整流程示例

### 每帧更新顺序

```cpp
// 1. 输入系统
InputManager::Update();  // 读取WASD和鼠标

// 2. 重力系统（Physics）
GravitySystem::Update(deltaTime, registry);
// - 计算每个对象受到的重力方向和强度
// - 更新 GravityAffectedComponent

// 3. 玩家对齐系统（Gameplay）
PlayerAlignmentSystem::Update(deltaTime, registry);
// - 根据重力方向调整玩家姿态
// - 平滑旋转到"脚朝地心"的方向

// 4. 玩家移动系统（Gameplay）
PlayerSystem::Update(deltaTime, registry);
// - 基于玩家局部坐标系处理WASD移动
// - 更新相机旋转（相对玩家）
// - 应用到PhysX角色控制器

// 5. 物理系统（Physics）
PhysicsSystem::Update(deltaTime, registry);
// - PhysX模拟（考虑重力）
// - 同步物理变换到Transform

// 6. 渲染系统（Graphics）
RenderSystem::Update(deltaTime, registry);
// - 相机始终跟随玩家
```

---

## 🌍 多星球支持

### 重力源优先级

```cpp
entt::entity FindNearestGravitySource(registry, playerPos) {
    entt::entity nearest = entt::null;
    float minDistance = FLT_MAX;
    
    auto sources = registry.view<GravitySourceComponent, TransformComponent>();
    for (auto entity : sources) {
        auto& source = sources.get<GravitySourceComponent>(entity);
        auto& transform = sources.get<TransformComponent>(entity);
        
        float distance = length(playerPos - transform.position);
        
        // 考虑影响半径（星球半径 + 大气层）
        float influenceRadius = source.radius + source.atmosphereHeight;
        
        if (distance < influenceRadius && distance < minDistance) {
            nearest = entity;
            minDistance = distance;
        }
    }
    
    return nearest;
}
```

### 星球间过渡

```cpp
// 当玩家在两个星球的影响范围重叠时，平滑过渡
if (newGravitySource != currentGravitySource) {
    float transitionDuration = 2.0f;  // 2秒过渡
    
    // 重力方向和强度的Lerp
    gravityDir = Lerp(oldGravityDir, newGravityDir, transitionProgress);
    gravityStrength = Lerp(oldStrength, newStrength, transitionProgress);
}
```

---

## 🐛 常见问题和解决方案

### 1. 玩家在球面上"抖动"
**原因**：对齐速度太快或PhysX角色控制器与重力冲突
**解决**：
- 降低 `alignmentSpeed`（建议 3-5）
- 使用 `XMQuaternionSlerp` 平滑插值
- PhysX重力设为0，手动控制重力

### 2. 相机旋转不对
**原因**：使用了世界空间轴而非玩家局部空间
**解决**：
- 所有旋转基于玩家的 `up/right/forward`
- Yaw = 绕玩家up轴，Pitch = 绕玩家right轴

### 3. 移动方向错误
**原因**：移动使用了固定的世界坐标轴
**解决**：
- WASD移动始终基于玩家的局部坐标系
- `forward = playerForward`, 不是 `{0,0,1}`

### 4. 穿过星球表面
**原因**：PhysX碰撞体未正确设置
**解决**：
- 星球需要球形碰撞体（Static）
- 使用 `PxSphereGeometry(radius)`
- 确保玩家初始位置在表面之上

---

## 📊 性能优化

### 1. 空间划分
```cpp
// 不要每帧遍历所有星球
// 使用八叉树或简单的距离剔除
if (distance > maxInfluenceDistance) continue;
```

### 2. 缓存最近的星球
```cpp
// 只在星球切换时重新计算
if (currentSource != entt::null) {
    float distToCurrentSource = GetDistance(player, currentSource);
    if (distToCurrentSource < currentSource.radius * 3) {
        // 仍在当前星球影响范围内，不需要查找
        return currentSource;
    }
}
```

### 3. 固定时间步
```cpp
// 重力计算可以使用较低的更新频率
if (gravityUpdateAccumulator > 0.1f) {  // 10Hz更新重力
    UpdateGravity();
    gravityUpdateAccumulator = 0;
}
```

---

## 🎯 实现优先级

### Sprint 1: 核心重力（1-2天）
- [x] GravitySourceComponent
- [x] GravityAffectedComponent
- [x] GravitySystem基础实现
- [x] 单星球重力测试

### Sprint 2: 玩家对齐（1天）
- [x] PlayerAlignmentComponent
- [x] PlayerAlignmentSystem
- [x] 玩家姿态平滑对齐

### Sprint 3: 控制器修改（1天）
- [x] 局部空间WASD移动
- [x] 局部空间相机旋转
- [x] 完整测试

### Sprint 4: 多星球（0.5天）
- [x] 重力源查找优化
- [x] 星球切换平滑过渡

---

## 🧪 测试场景

### 测试1: 单星球行走
```cpp
// 创建一个大球体星球
ModelConfig planet;
planet.name = "test_planet";
planet.objPath = "assets/BlendObj/planet1.obj";
planet.position = {0, 0, 0};
planet.scale = {20, 20, 20};  // 半径20米
planet.collisionType = CollisionType::Static;
planet.colliderShape = "Sphere";
planet.colliderSize = {20, 0, 0};

// 添加重力源组件
GravitySourceComponent gravitySource;
gravitySource.radius = 20.0f;
gravitySource.surfaceGravity = 9.8f;
registry.emplace<GravitySourceComponent>(planetEntity, gravitySource);

// 玩家放在星球表面
player.position = {0, 21, 0};  // 半径+1米
```

### 测试2: 环球行走
- WASD移动应该沿球面
- 相机应该始终"向上"指向外太空
- 可以360度绕球行走

### 测试3: 跳跃和坠落
- 跳跃应该离开星球表面
- 坠落应该朝向星球中心
- 落地后恢复正常站立

---

## 📚 参考资料

- [Outer Wilds GDC Talk](https://www.youtube.com/watch?v=LbY0mBXKKT0)
- Super Mario Galaxy 重力系统
- Unity CharacterController on Spheres
