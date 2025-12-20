# 参考系系统重设计方案

## 问题分析

### 当前实现的缺陷
1. **物理被禁用** - Kinematic 模式导致：
   - 无法施加力（重力、推力等）
   - 碰撞响应无效
   - 摩擦力无效
   - 所有动态物理都被冻结

2. **设计与现实不符**
   - 参考系不应该禁用物理
   - 应该改变坐标计算方式，但保持物理活跃

## 参考：《星际拓荒》的实现

在《星际拓荒》中：
- 玩家站在行星上 ≈ 玩家附着到行星的参考系
- 行星在旋转 → 玩家获得行星旋转的速度
- 但玩家仍能跳跃、被重力影响、与环境互动
- 切换参考系时 ≈ 速度场的突然变化，而非物理禁用

## 推荐方案：速度同步法

### 核心原理

```
玩家相对于世界的速度 = 参考系速度 + 玩家相对参考系的速度
```

### 实现步骤

#### 1. **保持物理启用**（不使用Kinematic）
- 所有刚体保持Dynamic模式
- PhysX继续计算碰撞、摩擦、重力等

#### 2. **计算参考系速度**
```cpp
reference_velocity = (current_position - last_position) / deltaTime
```

#### 3. **同步本地速度**（关键）
当物体附着到参考系时：
```cpp
local_velocity = world_velocity - reference_velocity
// 然后设置
world_velocity = reference_velocity + local_velocity
```

#### 4. **定期调整位置**
```cpp
// 基于参考系 + 局部坐标保持位置一致性
world_position = reference_position + local_position
```

#### 5. **仅在完全静止时轻微优化**
- 可以启用睡眠（sleep）以提高性能
- 但不要切换到Kinematic

### 优势
✅ 物理完全活跃
✅ 碰撞、摩擦正常工作
✅ 可以使用所有物理特性
✅ 参考系切换更平滑
✅ 与游戏逻辑一致

### 实现时间表
1. 移除 `ManageKinematicMode()` 函数
2. 创建 `SyncReferenceFrameVelocities()` 函数
3. 修改 `ApplyReferenceFrameMotion()` 以实现速度同步
4. 更新重力系统（不再需要跳过Kinematic检查）

## 过渡期间的注意事项

### 可能的滑动问题
- **原因**：参考系运动时，物体可能相对滑动
- **解决**：在`ApplyReferenceFrameMotion()`中增加阻尼或约束

### 浮点精度问题
- **原因**：持续的坐标同步可能累积浮点误差
- **解决**：定期校正（每帧或每几帧）而非每次都完全同步

## 代码架构变更

### 旧流程（有缺陷）
```
Update() {
  UpdateReferenceFrameVelocities()        // 计算参考系速度
  AutoAttachObjects()                     // 附着物体
  ApplyReferenceFrameMotion()            // 同步位置
  ManageKinematicMode()     ← ❌ 禁用物理
}
```

### 新流程（改进）
```
Update() {
  UpdateReferenceFrameVelocities()        // 计算参考系速度
  AutoAttachObjects()                     // 附着物体
  SyncReferenceFrameVelocities()         // ← NEW: 速度同步
  ApplyReferenceFrameMotion()            // 同步位置（保持 Dynamic）
  // 不再需要 ManageKinematicMode()
}
```

## 实现优先级

### Phase 1（立即）
- [ ] 移除 Kinematic 切换代码
- [ ] 验证不会立即出现滑动问题

### Phase 2（短期）
- [ ] 实现速度同步函数
- [ ] 测试参考系切换时的速度变化

### Phase 3（优化）
- [ ] 添加阻尼以防止滑动
- [ ] 性能优化（睡眠、批处理等）
