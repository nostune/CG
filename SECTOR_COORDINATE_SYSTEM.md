# 扇区坐标系统 (Sector Coordinate System)

## 核心架构

### 三层坐标系
1. **PhysX局部坐标**：物理模拟在扇区局部坐标系运行（原点=扇区中心）
2. **Transform世界坐标**：渲染系统使用的全局坐标
3. **扇区变换矩阵**：localToWorld/worldToLocal，连接局部和世界坐标

### 关键组件
- **SectorComponent**：扇区中心（星球）
  - PhysX位置：固定在(0,0,0)
  - Transform.position：世界坐标（轨道位置）
  - 维护变换矩阵：localToWorld, worldToLocal
  
- **SectorAffiliationComponent**：扇区隶属实体（飞船、物体）
  - currentSector：所属扇区Entity
  - localPosition：在扇区中的局部坐标
  - PhysX使用局部坐标，Transform由SectorSystem自动更新为世界坐标

## 坐标转换流程

### SectorSystem::Update（每帧执行）

1. **UpdateSectorTransforms()**
   - 根据星球Transform.position更新扇区变换矩阵
   - 构建localToWorld和worldToLocal矩阵

2. **UpdateRenderPositions()**
   - 遍历所有SectorAffiliationComponent实体
   - 读取PhysX Actor局部位置（CharacterController或RigidBody）
   - 使用localToWorld矩阵转换为世界坐标
   - 更新Transform.position供渲染使用

3. **ProcessSectorSwitching()**
   - 检测实体是否离开扇区影响范围
   - 自动切换到新扇区并转换坐标

### PhysicsSystem规则

**关键跳过逻辑**：
- `SyncTransformsToPhysics()`：跳过SectorComponent和SectorAffiliationComponent
- `SyncPhysicsToTransforms()`：跳过SectorComponent和SectorAffiliationComponent
- 原因：避免覆盖SectorSystem维护的坐标转换关系

## 创建实体指南

### 星球/扇区中心
```cpp
// 创建模型（PhysX位置在局部原点）
entt::entity planet = LoadModelAsEntity(
    registry, scene, device,
    "planet.obj", "texture.jpg",
    DirectX::XMFLOAT3(0, 0, 0),  // PhysX局部原点
    DirectX::XMFLOAT3(10, 10, 10),
    &physicsOptions
);

// 添加扇区组件
auto& sector = registry.emplace<SectorComponent>(planet);
sector.name = "Earth";
sector.influenceRadius = 84.0f;

// 添加重力源
auto& gravity = registry.emplace<GravitySourceComponent>(planet);

// 设置世界坐标（轨道位置）
auto& transform = registry.get<TransformComponent>(planet);
transform.position = {100, 0, 0};

// 强制PhysX Actor到原点
if (auto* rb = registry.try_get<RigidBodyComponent>(planet)) {
    physx::PxTransform origin(physx::PxVec3(0,0,0));
    rb->physxActor->setGlobalPose(origin);
}
```

### 物理实体（飞船/刚体）
```cpp
// 计算局部坐标（相对扇区中心）
DirectX::XMFLOAT3 localPos(18.8f, 62.7f, 0);

// 创建模型
entt::entity spacecraft = LoadModelAsEntity(
    registry, scene, device,
    "spacecraft.fbx", "",
    localPos,  // PhysX局部坐标
    DirectX::XMFLOAT3(0.02f, 0.02f, 0.02f),
    &physicsOptions
);

// 添加扇区隶属组件
auto& affiliation = registry.emplace<SectorAffiliationComponent>(spacecraft);
affiliation.currentSector = planetEntity;
affiliation.localPosition = localPos;
affiliation.autoSectorSwitch = true;
affiliation.isInitialized = true;

// 添加重力影响
registry.emplace<GravityAffectedComponent>(spacecraft);
```

### 玩家
```cpp
// CharacterController使用局部坐标
DirectX::XMFLOAT3 playerLocalPos(0, 65.8f, 0);
physx::PxCapsuleControllerDesc desc;
desc.position = physx::PxExtendedVec3(
    playerLocalPos.x, playerLocalPos.y, playerLocalPos.z
);

// 添加扇区隶属组件
auto& affiliation = registry.emplace<SectorAffiliationComponent>(player);
affiliation.currentSector = planetEntity;
affiliation.localPosition = playerLocalPos;

// 视觉模型（不添加SectorAffiliationComponent）
entt::entity visualModel = LoadModelAsEntity(
    registry, scene, device,
    "player.obj", "",
    playerLocalPos,
    DirectX::XMFLOAT3(0.25f, 0.25f, 0.25f),
    nullptr  // 无物理
);
// PlayerSystem手动管理视觉模型的Transform世界坐标
```

### 纯视觉模型（无物理）
```cpp
// 不添加SectorAffiliationComponent
// 由父系统手动管理Transform世界坐标
entt::entity model = LoadModelAsEntity(..., nullptr);
// 在Update中手动设置transform.position为世界坐标
```

## 重要规则

1. **扇区中心**：PhysX永远在(0,0,0)，Transform存储世界坐标
2. **扇区隶属实体**：PhysX存局部坐标，Transform由SectorSystem自动更新
3. **PhysicsSystem不干预**：跳过所有扇区相关实体的同步
4. **视觉模型**：如果没有物理组件，不要添加SectorAffiliationComponent
5. **相机Up向量**：必须转换到世界坐标系，保持pitch旋转正确
