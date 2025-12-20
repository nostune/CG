# 天体跟随系统 (Celestial Follow System)

## 问题背景

**核心问题**：当星球通过轨道运动移动时，站在星球上的玩家不会自动跟随，导致玩家"悬空"或被抛下。

**表现**：
- 月球公转时，站在月球上的玩家留在原地
- 地球公转时（测试模式），玩家被抛在后面
- 自由相机模式下，玩家身体不跟随星球移动

## v0.3.1 解决方案

### 实现方式

**核心组件**：
- `StandingOnComponent`：记录玩家站在哪个天体上，以及该天体的上一帧位置
- `CelestialFollowSystem`：追踪天体位移并应用到玩家

**系统流程**：
1. `OrbitSystem` 更新天体位置
2. `PlayerSystem` 处理玩家输入和移动（基于CharacterController）
3. `CelestialFollowSystem` 计算天体位移，通过 `setFootPosition()` 传送玩家

**关键设计决策**：
- 使用**位移追踪**而非**相对坐标**（避免被PlayerSystem覆盖Transform的问题）
- 在PlayerSystem**之后**运行（让PlayerSystem先完成正常移动逻辑）
- 始终同步视觉模型位置（即使在自由相机模式下）

### 代码位置
```
src/gameplay/components/StandingOnComponent.h    # 组件定义
src/gameplay/CelestialFollowSystem.h             # 系统实现
src/core/Engine.cpp (line 56)                    # 系统注册顺序
```

## 更好的解决方案

### 方案A：父子层级变换 (Scene Graph)
**原理**：实现完整的父子变换系统，子实体自动继承父实体的变换
```
地球 (Transform)
 └─ 玩家 (Transform, parent=地球)
     └─ 视觉模型 (Transform, parent=玩家)
```

**优点**：
- 更符合直觉的层级结构
- 自动处理多层嵌套（飞船内的玩家）
- 易于扩展到复杂场景

**缺点**：
- 需要重构整个Transform系统
- PhysX CharacterController与层级变换集成复杂

### 方案B：参考系管理器 (Reference Frame Manager)
**原理**：《Outer Wilds》原版使用的方案，维护多个局部参考系

**特点**：
- 每个天体是一个独立参考系
- 玩家在参考系间切换时进行坐标转换
- 物理模拟在局部参考系内进行

**优点**：
- 适合大尺度空间游戏（避免浮点精度问题）
- 物理计算更稳定
- 支持复杂的嵌套参考系（飞船内部、飞船轨道、星球轨道）

**缺点**：
- 实现复杂度高
- 需要处理参考系切换的边界情况

## 飞船系统扩展方案

### 场景：飞船围绕星球轨道运行

**推荐方案：扩展现有系统**

1. **给飞船添加组件**：
   ```cpp
   // 飞船实体
   auto& shipOrbit = registry.emplace<OrbitComponent>(shipEntity);
   auto& shipGravity = registry.emplace<GravityAffectedComponent>(shipEntity);
   
   // 飞船是一个"移动平台"
   auto& shipPlatform = registry.emplace<MovingPlatformComponent>(shipEntity);
   ```

2. **玩家进入飞船**：
   ```cpp
   // 玩家的StandingOnComponent.standingOnEntity = shipEntity
   // CelestialFollowSystem会自动处理玩家跟随飞船
   ```

3. **嵌套跟随**（飞船在轨道上，玩家在飞船上）：
   ```
   地球 → 飞船 (OrbitSystem移动)
            ↓
          玩家 (CelestialFollowSystem跟随飞船)
   ```

**实现要点**：
- 飞船内部使用局部坐标系（相对飞船中心）
- 检测玩家进入/离开飞船的触发器
- 处理飞船与星球的相对运动（双重参考系）

**进阶：飞船内部空间**
- 方案1：飞船整体作为StandingOn目标（当前系统已支持）
- 方案2：实现完整的参考系切换（更复杂但更真实）

## 当前限制

- 只支持一层跟随关系（不支持"玩家在飞船上，飞船在大飞船上"）
- 使用传送而非物理驱动（可能导致轻微的视觉抖动）
- 假设天体移动是连续的（瞬移式传送会出错）

## 测试配置

```cpp
// main.cpp - 测试地球轨道
planetOrbit.radius = 100.0f;  // 100米轨道半径
planetOrbit.period = 60.0f;   // 60秒一圈
```

移除测试轨道后，系统对月球公转仍然有效。
