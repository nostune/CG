# 参考系统与重力对齐机制总结

**日期**: 2025-12-21  
**版本**: v0.4.0

---

## 系统概述

当前实现了一套完整的**参考系统 (Reference Frame System)** 和**重力对齐机制 (Gravity Alignment Mechanism)**，用于在球形星球上模拟真实的物理交互，同时支持星球公转等复杂场景。

---

## 核心工作原理

### 1. 参考系统 (ReferenceFrameSystem)

**目标**: 解决星球公转时，物体相对星球位置保持稳定的问题。

**工作流程**:
```
物理更新前 (PrePhysicsUpdate):
  ├─ 更新参考系状态（记录星球位置/速度）
  ├─ 自动附着物体到重力源
  └─ 应用参考系移动（将物体从本地坐标转换到世界坐标）

物理模拟 (PhysX Simulation)
  └─ 在世界坐标下进行物理计算

物理更新后 (PostPhysicsUpdate):
  └─ 更新本地坐标（将物体世界位置转回本地坐标）
```

**关键技术**:
- **本地坐标存储**: 物体位置以"相对星球中心的偏移"存储在 `AttachedToReferenceFrameComponent.localPosition`
- **坐标转换**: 每帧进行本地↔世界坐标转换，确保物体跟随星球移动
- **误差消除**: 使用本地坐标存储避免了弧线运动与直线近似的累积误差

**数学公式**:
```
世界坐标 = 参考系位置 + 本地坐标
本地坐标 = 世界坐标 - 参考系位置
```

---

### 2. 重力对齐机制 (Gravity Alignment)

**目标**: 让PhysX正确处理球面重力环境中的碰撞和摩擦。

**核心原理**:

PhysX的刚体物理基于**全局坐标系**，默认重力向下（-Y方向）。在球形星球上：
- 星球表面每个点的"重力方向"都不同（指向球心）
- 如果物体局部Y轴不对齐重力反方向，PhysX会认为物体在"倾斜面"上
- 重力的切向分量会导致物体滑动

**解决方案**: 通过旋转物体，使其**局部Y轴始终对齐重力反方向**，让PhysX误以为物体在标准重力下的平地上。

**对齐计算** (ReferenceFrameSystem.h:280-320):
```cpp
// 1. 计算重力反方向（应该朝向的方向）
XMVECTOR upDir = XMVectorNegate(XMLoadFloat3(&gravityDir));

// 2. 计算旋转四元数：从世界Y轴 旋转到 重力反方向
XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
XMVECTOR axis = XMVector3Cross(worldUp, upDir);
float dot = XMVectorGetX(XMVector3Dot(worldUp, upDir));
float angle = acosf(dot);
XMVECTOR rotQuat = XMQuaternionRotationAxis(axis, angle);

// 3. 可选的平滑过渡（SLERP）
if (alignmentSpeed > 0.0f) {
    float t = min(1.0f, alignmentSpeed * deltaTime);
    rotQuat = XMQuaternionSlerp(currentRotQuat, rotQuat, t);
}

// 4. 应用到PhysX刚体
actor->setGlobalPose(PxTransform(pos, PxQuat(rotQuat)));
```

---

### 3. 灵活的对齐模式 (GravityAlignmentComponent)

为不同类型物体提供三种对齐策略：

| 模式 | 用途 | 行为 | 示例 |
|------|------|------|------|
| **NONE** | 完全自由旋转 | 不进行任何对齐 | 飞行中的飞船 |
| **FORCE_ALIGN** | 静态物体 | 每帧强制对齐到重力反方向 | 箱子、石头、测试刚体 |
| **ASSIST_ALIGN** | 动态载具 | 玩家激活时辅助对齐（降落辅助） | 着陆中的飞船 |

**配置参数**:
```cpp
struct GravityAlignmentComponent {
    AlignmentMode mode;              // 对齐模式
    float alignmentSpeed;            // 0=瞬间对齐, >0=SLERP平滑过渡
    bool alignOnlyWhenGrounded;      // 是否仅在接地时对齐
    bool assistAlignActive;          // ASSIST_ALIGN模式下的激活状态
};
```

---

## 为什么这样设计？

### PhysX架构限制

1. **CharacterController** 独有的 `setUpDirection()` 方法：
   - 支持设置"上方向"，PhysX内部用于计算坡面角度
   - 自动处理球面重力环境

2. **RigidDynamic/RigidStatic** 不支持per-actor的"上方向"：
   - 碰撞和接触求解基于全局坐标系
   - 摩擦力方向由接触法线决定，不考虑重力方向

**因此**: 强制旋转对齐是在刚体物理中模拟球面重力的**必要变通方案**，而非bug。

---

## 远距离物体的处理策略

### 问题背景

在大型太空场景中，远离玩家的物体可能面临：
1. **浮点精度问题**: 坐标值过大导致精度丢失
2. **性能问题**: 大量远距离物体消耗计算资源
3. **物理稳定性**: 距离过远时物理模拟可能不稳定

### 解决方案

#### 方案1: 层次化参考系（推荐用于Outer Wilds风格）

```
太阳系统 (Solar System)
  ├─ 太阳 [参考系: 绝对坐标]
  │
  ├─ 地球 [参考系: 相对太阳]
  │   ├─ 玩家 [参考系: 相对地球]
  │   ├─ 建筑物 [参考系: 相对地球]
  │   └─ 月球 [参考系: 相对地球]
  │
  └─ 火星 [参考系: 相对太阳]
      └─ 火星基地 [参考系: 相对火星]
```

**实现要点**:
```cpp
// ReferenceFrameComponent 添加父引用
struct ReferenceFrameComponent {
    std::string name;
    bool isActive;
    entt::entity parentFrame;  // 新增：父参考系
    XMFLOAT3 localPosition;    // 相对父参考系的位置
};

// 递归计算世界坐标
XMFLOAT3 GetWorldPosition(entt::entity entity) {
    auto* refFrame = GetComponent<ReferenceFrameComponent>(entity);
    if (!refFrame || refFrame->parentFrame == entt::null) {
        return GetComponent<TransformComponent>(entity)->position;
    }
    
    XMVECTOR parentPos = XMLoadFloat3(&GetWorldPosition(refFrame->parentFrame));
    XMVECTOR localPos = XMLoadFloat3(&refFrame->localPosition);
    XMVECTOR worldPos = XMVectorAdd(parentPos, localPos);
    
    XMFLOAT3 result;
    XMStoreFloat3(&result, worldPos);
    return result;
}
```

**优势**:
- 每个星球内部使用本地坐标，精度高
- 自然处理多星球场景
- 符合Outer Wilds的设计哲学

#### 方案2: 浮动原点 (Floating Origin)

将整个世界坐标系的原点移动到玩家附近：

```cpp
void UpdateFloatingOrigin() {
    XMFLOAT3 playerPos = GetPlayerPosition();
    float distanceThreshold = 5000.0f;  // 5公里
    
    float distance = sqrt(playerPos.x * playerPos.x + 
                         playerPos.y * playerPos.y + 
                         playerPos.z * playerPos.z);
    
    if (distance > distanceThreshold) {
        XMVECTOR offset = XMVectorNegate(XMLoadFloat3(&playerPos));
        
        // 移动所有实体
        for (auto entity : AllEntities()) {
            auto* transform = GetComponent<TransformComponent>(entity);
            XMVECTOR pos = XMLoadFloat3(&transform->position);
            pos = XMVectorAdd(pos, offset);
            XMStoreFloat3(&transform->position, pos);
        }
        
        // 同步PhysX场景
        SyncPhysXActors();
    }
}
```

**优势**:
- 简单直接，不需要修改现有架构
- PhysX模拟始终在原点附近，精度高

**劣势**:
- 每次重定位开销大
- 需要处理所有物体（包括UI、粒子等）

#### 方案3: 区域分割 + LOD（适合大型开放世界）

```cpp
struct SpatialRegion {
    XMFLOAT3 center;
    float radius;
    std::vector<entt::entity> entities;
    bool physicsActive;  // 是否激活物理模拟
};

void UpdateRegions() {
    XMFLOAT3 playerPos = GetPlayerPosition();
    
    for (auto& region : regions) {
        float distance = Distance(playerPos, region.center);
        
        if (distance < 500.0f) {
            // 近距离：完整物理模拟
            region.physicsActive = true;
        } else if (distance < 2000.0f) {
            // 中距离：简化物理（休眠刚体）
            region.physicsActive = false;
            for (auto entity : region.entities) {
                auto* rb = GetRigidBody(entity);
                if (rb) rb->putToSleep();
            }
        } else {
            // 远距离：仅更新轨道，不模拟物理
            region.physicsActive = false;
            UpdateOrbitOnly(region.entities);
        }
    }
}
```

**优势**:
- 性能优化显著
- 适合大规模场景

**劣势**:
- 实现复杂
- 需要处理区域边界问题

---

## 推荐架构（针对Outer Wilds风格游戏）

```
1. 使用层次化参考系（方案1）
   - 每个星球/月球作为独立参考系
   - 物体附着到最近的天体

2. 结合距离LOD
   - 可见距离内：完整渲染 + 完整物理
   - 影响距离内：简化渲染 + 轨道更新
   - 超远距离：不渲染 + 仅记录状态

3. 智能激活策略
   - 玩家所在星球：全激活
   - 相邻星球：激活关键物体（NPC、任务物品）
   - 其他星球：休眠，仅保持轨道运行
```

**代码示例**:
```cpp
void UpdatePhysicsActivation() {
    auto* playerFrame = GetPlayerReferenceFrame();
    
    for (auto [entity, refFrame, rigidBody] : EntitiesWithPhysics()) {
        // 同一参考系：全激活
        if (refFrame.referenceFrame == playerFrame) {
            rigidBody.physxActor->wakeUp();
            continue;
        }
        
        // 不同参考系：检查距离
        float distance = DistanceBetweenFrames(refFrame.referenceFrame, playerFrame);
        if (distance > 10000.0f) {
            rigidBody.physxActor->putToSleep();
        }
    }
}
```

---

## 总结

**当前实现的优势**:
- ✅ 完美支持星球公转时物体相对位置稳定
- ✅ 正确处理球面重力环境中的碰撞和摩擦
- ✅ 灵活的对齐策略支持不同物体类型
- ✅ 无累积误差（本地坐标存储）

**适用场景**:
- 单一星球或少量星球的小规模场景
- 玩家始终处于某个星球影响范围内

**扩展方向**:
- 实现层次化参考系以支持多星球场景
- 添加距离LOD和智能激活策略以优化性能
- 考虑浮动原点以处理极大规模场景

---

## 相关文档

- [参考系统重新设计](./REFERENCE_FRAME_REDESIGN.md) - 详细设计文档
- [重力系统设计](./PLANETARY_GRAVITY_DESIGN.md) - 球面重力实现
- [引擎架构](./ENGINE_ARCHITECTURE.md) - 系统集成说明
