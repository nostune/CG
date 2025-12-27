# Sector 系统物体类型设计

## 概述

在 Sector 坐标系统中，所有物体都需要正确处理局部坐标与世界坐标的转换。不同类型的物体有不同的物理行为和坐标处理方式。

## 物体类型分类

### 1. SectorEntityType 枚举

```cpp
enum class SectorEntityType {
    Player,           // 玩家 - CharacterController，受重力影响
    StaticObject,     // 静态物体 - 建筑、地形装饰等，不移动
    SimulatedObject,  // 可模拟物体 - 受物理影响的动态物体（箱子、石头等）
    Spacecraft,       // 飞船 - 可切换静态/可模拟模式
    Celestial,        // 天体 - 星球、月球等，是 Sector 的宿主
    Other             // 其他 - 不参与 Sector 物理的物体
};
```

### 2. 各类型详细说明

#### Player（玩家）
- **PhysX 表示**: `PxController` (CharacterController)
- **坐标处理**: 
  - 位置存储在 `InSectorComponent.localPosition`
  - CharacterController 在局部坐标系中运行
  - 渲染时转换为世界坐标
- **重力**: 受 Sector 重力影响，方向指向 Sector 中心
- **Sector 切换**: 根据距离自动切换到最近的 Sector
- **已实现**: ✅

#### StaticObject（静态物体）
- **PhysX 表示**: `PxRigidStatic`
- **坐标处理**:
  - 创建时在局部坐标系中定位
  - PhysX 碰撞体在局部坐标
  - 渲染时转换为世界坐标
- **重力**: 不受重力影响（静态）
- **Sector 切换**: 固定属于创建时的 Sector，不切换
- **用例**: 建筑物、树木、岩石、地面装饰

#### SimulatedObject（可模拟物体）
- **PhysX 表示**: `PxRigidDynamic`
- **坐标处理**:
  - PhysX 在局部坐标系中模拟
  - 每帧从 PhysX 读取局部位置
  - 渲染时转换为世界坐标
- **重力**: 受 Sector 重力影响
- **Sector 切换**: 可以根据距离切换 Sector
- **用例**: 可推动的箱子、可捡起的物品、抛射物

#### Spacecraft（飞船）
- **PhysX 表示**: `PxRigidDynamic`，可切换 kinematic
- **模式**:
  - **停泊模式**: kinematic=true，相对于 Sector 静止
  - **飞行模式**: kinematic=false，完全动态模拟
- **坐标处理**:
  - 停泊时：固定在 Sector 局部坐标
  - 飞行时：在世界坐标或当前 Sector 局部坐标模拟
- **重力**: 飞行模式下受重力影响（可配置）
- **Sector 切换**: 飞行时可切换 Sector

#### Celestial（天体）
- **PhysX 表示**: `PxRigidStatic`（作为碰撞体）
- **特殊性**: 
  - 是 Sector 的宿主实体
  - 拥有 `SectorComponent`
  - 其 PhysX 碰撞体始终在局部坐标原点 (0,0,0)
- **坐标处理**:
  - `TransformComponent.position` 存储世界坐标（用于渲染和轨道计算）
  - PhysX 碰撞体固定在 (0,0,0)
  - 不被 PhysicsSystem 同步
- **已实现**: ✅

## 组件设计

### 现有组件（已实现）

```cpp
// Sector 宿主组件 - 附加到天体上
struct SectorComponent {
    std::string name;
    float influenceRadius;      // 影响范围
    int priority;               // 优先级
    bool isActive;              // 是否为当前活跃 Sector
    XMFLOAT3 worldPosition;     // 世界坐标（每帧同步）
    XMFLOAT4 worldRotation;     // 世界旋转
    bool physicsInitialized;    // PhysX 是否已初始化到原点
};

// 实体所属 Sector 组件
struct InSectorComponent {
    Entity currentSector;       // 当前所属 Sector
    Entity previousSector;      // 上一个 Sector（用于切换过渡）
    XMFLOAT3 localPosition;     // 局部坐标
    XMFLOAT4 localRotation;     // 局部旋转
    bool isInitialized;         // 是否已初始化
};
```

### 新增组件

```cpp
// Sector 实体类型组件 - 标识实体类型和行为
struct SectorEntityTypeComponent {
    SectorEntityType type;
    
    // 类型特定标志
    bool canSwitchSector;       // 是否可以切换 Sector
    bool affectedByGravity;     // 是否受重力影响
    bool syncPhysicsToRender;   // 是否同步物理到渲染
};

// 飞船特定组件（扩展现有 SpacecraftComponent）
struct SpacecraftModeComponent {
    enum class Mode {
        Parked,     // 停泊 - kinematic
        Flying      // 飞行 - dynamic
    };
    Mode currentMode;
    Entity parkedInSector;      // 停泊时所属的 Sector
};
```

## 系统处理流程

### SectorSystem::Update() 流程

```
1. SyncSectorWorldPositions()
   - 同步所有 Sector 的世界坐标

2. InitializeSectorPhysics()
   - 初始化 Sector 物理体到原点（仅一次）

3. ProcessEntityTypes()
   - 根据 SectorEntityType 处理不同类型实体
   
4. DetectSectorChanges()
   - 检测可切换 Sector 的实体是否需要切换

5. HandleSectorSwitches()
   - 处理 Sector 切换

6. InitializeNewEntities()
   - 初始化新加入 Sector 的实体
```

### SectorSystem::PostPhysicsUpdate() 流程

```
1. 遍历所有 InSectorComponent 实体
2. 根据 SectorEntityType 读取物理状态
   - Player: 从 CharacterController 读取
   - SimulatedObject: 从 RigidDynamic 读取
   - StaticObject: 不需要读取（静态）
   - Spacecraft: 根据模式读取
3. 计算世界坐标
4. 更新 TransformComponent（用于渲染）
```

## 实现计划

### Phase 1: 组件定义 ✅
- [x] SectorComponent
- [x] InSectorComponent  
- [x] SectorEntityTypeComponent (Player, StaticObject, SimulatedObject, Spacecraft, Celestial, Other)
- [x] SpacecraftModeComponent

### Phase 2: 静态物体支持 ✅
- [x] LoadStaticObjectInSector() 函数
- [x] 在局部坐标系中创建 PhysX 碰撞体
- [x] 正确渲染世界坐标（PostPhysicsUpdate 处理）

### Phase 3: 可模拟物体支持 🔄
- [ ] LoadSimulatedObjectInSector() 函数
- [ ] 动态物体的 Sector 内物理模拟
- [ ] 重力应用（指向 Sector 中心）
- [ ] Sector 切换处理

### Phase 4: 飞船支持 📋
- [ ] 停泊/飞行模式切换
- [ ] 玩家上下飞船时的坐标处理
- [ ] 飞船内部的相对坐标

### Phase 5: 完善与优化 📋
- [ ] 多 Sector 场景测试
- [ ] 性能优化
- [ ] 边界情况处理

## 坐标转换规则

### 局部坐标 → 世界坐标
```cpp
worldPos = sector.worldPosition + localPos
worldRot = sector.worldRotation * localRot  // 四元数乘法
```

### 世界坐标 → 局部坐标
```cpp
localPos = worldPos - sector.worldPosition
localRot = inverse(sector.worldRotation) * worldRot
```

### PhysX 坐标规则
- Sector 宿主（天体）的碰撞体：始终在 (0,0,0)
- Sector 内的实体：使用局部坐标
- 不在 Sector 中的实体：使用世界坐标

## 注意事项

1. **PhysicsSystem 排除规则**
   - Sector 实体（有 SectorComponent）不参与 transform ↔ physics 同步
   - 由 SectorSystem 专门管理

2. **重力方向**
   - 在 Sector 内：重力方向 = -normalize(localPosition)
   - 不在 Sector 内：使用传统世界坐标重力计算

3. **渲染坐标**
   - 所有实体的 TransformComponent.position 必须是世界坐标
   - RenderSystem 不需要知道 Sector 系统的存在
