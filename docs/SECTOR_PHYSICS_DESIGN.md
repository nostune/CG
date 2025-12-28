# 扇区物理系统设计

## 文件结构

```
src/physics/
├── PhysXManager.h/cpp              # [已有] PhysX 初始化、simulate/fetchResults
├── PhysicsSystem.h/cpp             # [已有] 基础 Transform ↔ PhysX 同步
├── CoordinateSystem.h              # [已有] 左右手坐标系转换工具
├── SectorPhysicsSystem.h/cpp       # [新建] 扇区物理核心系统
└── components/
    ├── RigidBodyComponent.h        # [已有] 刚体属性 + PhysX actor 指针
    ├── ColliderComponent.h         # [已有] 碰撞体形状定义
    ├── GravitySourceComponent.h    # [已有] 重力源（星球属性）
    ├── GravityAffectedComponent.h  # [已有] 受重力影响标记
    └── SectorComponent.h           # [新建] 扇区定义 + 扇区内实体标记
```

## 组件职责

### SectorComponent.h
- `SectorComponent`: 扇区定义，附加到星球，包含世界位置、影响半径、地面碰撞体
- `InSectorComponent`: 扇区内实体，附加到玩家/物体，包含所属扇区、局部坐标

### SectorPhysicsSystem.h/cpp
- `PrePhysicsUpdate()`: 计算重力、应用力、同步 Kinematic
- `PostPhysicsUpdate()`: 读取 PhysX 结果、局部→世界坐标转换、更新 Transform
- `TransferEntityToSector()`: 实体在扇区间切换

## 更新顺序

```
Engine::Update():
  1. PlayerSystem (输入)
  2. SectorPhysicsSystem::PrePhysicsUpdate (重力、力)
  3. PhysXManager::Update (simulate/fetchResults)
  4. SectorPhysicsSystem::PostPhysicsUpdate (坐标转换)
  5. CameraSystem
  6. RenderSystem
```
