# 扇区系统设计文档 (Sector System Design)

## 概述

本文档描述了基于《星际拓荒》(Outer Wilds) 参考系解决方案的扇区系统设计。该系统旨在取代原有的纯物理参考系补偿方法，提供更稳定、更直观的多天体运动支持。

## 目录

1. [核心问题](#核心问题)
2. [星际拓荒的解决方案](#星际拓荒的解决方案)
3. [系统架构](#系统架构)
4. [组件设计](#组件设计)
5. [系统流程](#系统流程)
6. [迁移指南](#迁移指南)
7. [常见问题](#常见问题)

---

## 核心问题

### 为什么纯物理方法行不通？

在太空游戏中，玩家需要能够站在运动的天体（如公转中的行星）上，并且：
- 自然地跟随天体运动
- 正常进行物理交互（行走、跳跃、碰撞）
- 在天体之间平滑过渡

**纯物理方法的问题：**

| 问题 | 描述 |
|------|------|
| 惯性力补偿复杂 | 需要计算科里奥利力、离心力等虚拟力 |
| 浮点精度问题 | 远距离坐标（如10000+米）会产生抖动 |
| 速度继承困难 | 跳跃/离开天体时速度继承计算复杂 |
| 物理引擎限制 | PhysX在非惯性系中的行为不可预测 |
| 调试困难 | 问题来源难以定位 |

---

## 星际拓荒的解决方案

### 核心思想：局部坐标系

```
                        【世界空间】
                             |
           +-----------------+-----------------+
           |                 |                 |
      【太阳扇区】       【行星扇区】       【月球扇区】
           |                 |                 |
      太阳表面物体      行星表面物体      月球表面物体
      (局部坐标)        (局部坐标)        (局部坐标)
```

### 关键概念

1. **扇区 (Sector)**
   - 每个天体定义一个独立的局部坐标系
   - 物体的位置相对于所属扇区原点存储
   - 天体运动对附着物体"透明"

2. **坐标存储方式**
   ```cpp
   // 旧方法：世界坐标
   position = {5000.0f, 100.0f, 3000.0f};  // 需要不断补偿
   
   // 新方法：局部坐标
   localPosition = {0.0f, 100.0f, 0.0f};   // 相对于行星原点
   ```

3. **渲染时的坐标转换**
   - 不直接使用世界坐标渲染
   - 计算相对于当前活动参考系（相机所在扇区）的位置
   - 避免浮点精度问题

---

## 系统架构

### 组件关系图

```
+------------------+       +-------------------------+
| SectorComponent  |       | SectorAffiliationComp   |
|------------------|       |-------------------------|
| - worldPosition  |       | - currentSector         |
| - worldRotation  |       | - localPosition         |
| - localToWorld   |  <--  | - localRotation         |
| - worldToLocal   |       | - autoSectorSwitch      |
| - influenceRadius|       +-------------------------+
+------------------+              |
        ^                         |
        |                         v
        |               +------------------+
        +---------------|  SectorSystem    |
                        |------------------|
                        | - UpdateTransforms|
                        | - ProcessSwitching|
                        | - SyncPhysics     |
                        +------------------+
```

### 更新顺序

```
1. OrbitSystem          → 更新天体世界位置
2. SectorSystem         → 更新扇区矩阵，处理切换
3. GravitySystem        → 计算重力方向
4. ApplyGravitySystem   → 施加重力力
5. PlayerSystem         → 处理玩家输入
6. PhysX Simulation     → 物理模拟
7. PostPhysicsUpdate    → 从物理结果更新局部坐标
8. RenderSystem         → 渲染（使用相对坐标）
```

---

## 组件设计

### SectorComponent

天体/飞船作为一个独立的参考系（扇区）：

```cpp
struct SectorComponent {
    // 标识
    std::string name;
    Type type;  // CELESTIAL_BODY, SPACECRAFT, SPACE
    
    // 世界坐标（扇区原点）
    XMFLOAT3 worldPosition;
    XMFLOAT4 worldRotation;
    
    // 变换矩阵（每帧更新）
    XMFLOAT4X4 localToWorld;
    XMFLOAT4X4 worldToLocal;
    
    // 边界（用于自动切换）
    float influenceRadius;  // 进入范围
    float exitRadius;       // 退出范围
    int priority;           // 优先级
};
```

### SectorAffiliationComponent

物体归属于某个扇区：

```cpp
struct SectorAffiliationComponent {
    // 所属扇区
    Entity currentSector;
    Entity previousSector;
    
    // 局部坐标
    XMFLOAT3 localPosition;
    XMFLOAT4 localRotation;
    XMFLOAT3 localVelocity;
    
    // 切换控制
    bool autoSectorSwitch;
    bool sectorLocked;
    float switchCooldown;
};
```

---

## 系统流程

### 扇区切换流程

```
玩家接近行星
     |
     v
检测是否进入影响范围
     |
     ├── 否 → 继续检测
     |
     └── 是 → 计算优先级
              |
              v
         选择最高优先级扇区
              |
              v
         坐标变换：
         worldPos = oldSector.LocalToWorld(localPos)
         newLocalPos = newSector.WorldToLocal(worldPos)
              |
              v
         更新 SectorAffiliation
         设置切换冷却
```

### 每帧更新流程

```cpp
void SectorSystem::Update(float dt, registry) {
    // 1. 更新扇区变换矩阵
    for (sector : sectors) {
        sector.localToWorld = BuildMatrix(sector.worldPosition, sector.worldRotation);
        sector.worldToLocal = Inverse(sector.localToWorld);
    }
    
    // 2. 处理扇区切换
    for (entity : affiliations) {
        if (ShouldSwitch(entity)) {
            ExecuteSwitch(entity, newSector);
        }
    }
    
    // 3. 同步Transform（用于渲染）
    for (entity : affiliations) {
        worldPos = sector.LocalToWorld(localPosition);
        transform.position = worldPos;
    }
}

void SectorSystem::PostPhysicsUpdate(registry) {
    // 物理模拟后，从世界坐标更新局部坐标
    for (entity : affiliations) {
        worldPos = GetPhysXPosition(entity);
        localPosition = sector.WorldToLocal(worldPos);
    }
}
```

---

## 迁移指南

### 从旧系统迁移

#### 1. 天体设置

**旧方法：**
```cpp
auto& refFrame = registry.emplace<ReferenceFrameComponent>(planetEntity);
refFrame.name = "Earth";
```

**新方法：**
```cpp
auto& sector = registry.emplace<SectorComponent>(planetEntity);
sector.name = "Earth";
sector.type = SectorComponent::Type::CELESTIAL_BODY;
sector.influenceRadius = 50.0f;
sector.exitRadius = 75.0f;
sector.priority = 10;
```

#### 2. 附着物体

**旧方法：**
```cpp
auto& attached = registry.emplace<AttachedToReferenceFrameComponent>(entity);
attached.autoAttach = true;
```

**新方法：**
```cpp
auto& affiliation = registry.emplace<SectorAffiliationComponent>(entity);
affiliation.autoSectorSwitch = true;

// 手动附着到扇区
sectorSystem->AttachToSector(entity, planetEntity, registry);
```

#### 3. 玩家/飞船

```cpp
// 玩家
auto& playerAffiliation = registry.emplace<SectorAffiliationComponent>(playerEntity);
playerAffiliation.autoSectorSwitch = true;

// 飞船（可锁定扇区）
auto& shipAffiliation = registry.emplace<SectorAffiliationComponent>(spacecraftEntity);
shipAffiliation.autoSectorSwitch = true;
// 飞行时锁定：
shipAffiliation.sectorLocked = true;
```

---

## 常见问题

### Q: 如何处理飞船起飞？

飞船起飞时：
1. 设置 `sectorLocked = true`
2. 飞船保持在当前扇区
3. 飞到新天体后，解锁并自动切换

### Q: 如何处理嵌套扇区（月球绕行星）？

使用 `parentSector` 字段：
```cpp
moonSector.parentSector = planetEntity;
```

系统会自动处理层级变换。

### Q: 物理交互如何工作？

- 物理模拟在**世界坐标**中进行
- `SectorSystem::Update()` 将局部坐标转为世界坐标同步给PhysX
- `PostPhysicsUpdate()` 将物理结果转回局部坐标

### Q: 性能如何？

相比纯物理方法：
- 矩阵乘法：每帧每物体2次 vs 复杂的力计算
- 无惯性力积分
- 无精度补偿迭代

---

## 文件清单

| 文件 | 描述 |
|------|------|
| `src/physics/components/SectorComponent.h` | 扇区组件定义 |
| `src/physics/SectorSystem.h` | 扇区系统实现 |
| `src/core/Engine.cpp` | 系统注册和更新 |
| `docs/SECTOR_SYSTEM_DESIGN.md` | 本设计文档 |

---

## 与星际拓荒的对应关系

| 星际拓荒概念 | 本实现 |
|------------|--------|
| Sector | `SectorComponent` |
| OWRigidbody.SetAttachedSector() | `SectorAffiliationComponent` |
| PlayerSectorDetector | `SectorSystem::ProcessSectorSwitching()` |
| GlobalMessenger.FireEvent(OWEvents.EnterSector) | `justSwitchedSector` 标志 |
| ReferenceFrame | 合并入 `SectorComponent` |

---

*文档版本: 1.0*
*最后更新: 2025年12月24日*
