# OuterWilds ECS 引擎 - 毕业设计核心技术亮点总结

本项目是一个基于 ECS (Entity-Component-System) 架构的高性能 3D 游戏引擎，复刻了《星际拓荒》(Outer Wilds) 的核心技术机制。项目重点解决了大规模宇宙场景下的物理模拟精度问题、实现了高效的渲染管线以及真实感的飞船驾驶体验。

以下是项目的四大核心技术优势详细介绍：

## 1. 现代 ECS 架构设计 (Architecture)

本项目摒弃了传统的面向对象继承体系，全面采用 **ECS (Entity-Component-System)** 架构，基于业界领先的 **EnTT v3.16.0** 库实现。

### 核心设计
*   **数据驱动 (Data-Oriented)**：将游戏对象拆解为纯数据组件（Component），逻辑由系统（System）统一处理。这种内存布局极大地提高了 CPU 缓存命中率。
*   **解耦与复用**：
    *   **Entity (实体)**：仅作为 ID 存在，不包含任何逻辑。
    *   **Component (组件)**：如 `TransformComponent`, `RigidBodyComponent`, `AudioComponent`，只包含数据。
    *   **System (系统)**：如 `PhysicsSystem`, `RenderSystem`, `AudioSystem`，负责处理拥有特定组件的实体。
*   **优势**：
    *   **高性能**：支持海量实体的并行更新，特别适合粒子系统和大规模场景。
    *   **灵活性**：可以在运行时动态地为实体添加或移除组件（例如，玩家进入飞船后，飞船实体获得 `PlayerControlComponent`）。

---

## 2. 扇区物理系统与多星球模拟 (Sector Physics & Gravity)

这是本项目最具技术挑战性的部分，旨在解决开放宇宙游戏中的**浮点精度问题**和**高速运动下的物理模拟不稳定性**。

### 2.1 扇区坐标系 (Sector Coordinate System) - 解决惯性系难题
在传统引擎中，如果飞船以极高速度移动，物理引擎会因为浮点数精度丢失而产生抖动。本项目采用了**相对坐标系**方案：

*   **扇区 (Sector) 概念**：每个星球及其周围空间被定义为一个独立的"扇区"。
*   **局部物理模拟**：
    *   PhysX 物理模拟永远在**当前扇区的局部坐标系**中进行。
    *   **星球中心**被固定为局部坐标原点 `(0, 0, 0)`。
    *   **星球是静止的**：在物理模拟层面，星球不移动，玩家和飞船相对于星球运动。这构建了一个完美的**惯性参考系**，彻底消除了高速运动带来的物理计算误差。
*   **坐标转换管线**：
    *   **Pre-Physics**：将实体的世界坐标转换为相对于当前扇区的局部坐标。
    *   **Physics Simulation**：PhysX 进行高精度的局部模拟。
    *   **Post-Physics**：将模拟结果（局部坐标）转换回世界坐标，供渲染系统使用。

### 2.2 动态重力与星球行走 (Planetary Walking)
为了实现类似《马里奥银河》或《星际拓荒》的任意表面行走体验：

*   **球形重力场**：每个星球都有 `GravitySourceComponent`，计算指向星球中心的引力向量。
*   **自适应角色控制器**：
    *   `PlayerSystem` 实时计算玩家脚下的重力方向。
    *   **动态坐标帧**：每一帧都根据重力方向重新构建玩家的 `LocalUp`, `LocalForward`, `LocalRight` 向量。
    *   这使得玩家可以在球体表面无缝行走，无论是在北极还是南极，操作手感完全一致，不会出现"万向节死锁"问题。

---

## 3. 高效渲染队列 (Render Queue)

为了在 DirectX 11 上实现高效渲染，设计了一套基于**排序键 (Sort Key)** 的渲染批处理系统。

### 3.1 渲染批次 (RenderBatch)
渲染不再是简单的遍历实体并绘制，而是将渲染指令封装为 `RenderBatch`：
*   包含所有必要的 GPU 资源句柄（VB/IB, Shaders, Textures）。
*   包含材质参数和变换矩阵。

### 3.2 64位排序键优化
为了最小化 GPU 状态切换（State Changes），每个渲染批次都会生成一个 64 位的 `SortKey`：

```cpp
// SortKey 结构: [RenderPass(8)] [Shader(8)] [Material(8)] [Depth(16)] [Reserved(24)]
uint64_t sortKey = (renderPass << 56) | (shaderId << 48) | (materialId << 40) | (depth << 24);
```

*   **RenderPass**：优先区分不透明（Opaque）和透明（Transparent）物体。
*   **Shader/Material**：将使用相同着色器和材质的物体排在一起，极大减少了 `SetShader` 和 `SetShaderResources` 的调用次数。
*   **Depth**：
    *   **不透明物体**：按从近到远排序（利用 Early-Z 剔除优化）。
    *   **透明物体**：按从远到近排序（保证正确的混合效果）。

---

## 4. 真实光影与 PBR 渲染管线 (Lighting & PBR)

项目实现了一套现代化的渲染管线，支持物理基础渲染 (Physically Based Rendering)。

### 4.1 PBR 材质工作流
支持完整的 PBR 纹理贴图，使物体在不同光照下表现出真实的物理特性：
*   **Albedo**：基础颜色。
*   **Normal**：法线贴图，增加表面细节凹凸感（配合切线空间计算）。
*   **Metallic**：金属度，控制反射特性。
*   **Roughness**：粗糙度，控制高光范围和反射模糊度。
*   **AO (Ambient Occlusion)**：环境光遮蔽，增加阴影深度。
*   **Emissive**：自发光，用于飞船仪表盘、星球大气等发光效果。

### 4.2 模型加载与资源管理
*   **GLB/GLTF 支持**：集成了 Assimp 库，支持加载现代 GLB 模型格式。
*   **自动化处理**：加载模型时自动提取嵌入式纹理，自动计算切线空间（Tangent Space），无需美术人员手动干预即可在引擎中正确显示法线效果。
*   **多光源支持**：支持定向光（模拟恒星光照）和点光源，配合 PBR 材质实现逼真的明暗变化。

---

## 总结
本项目不仅仅是一个简单的游戏 Demo，而是一个**架构完整、技术深度对标商业引擎**的开发实践。从底层的 ECS 内存管理，到中间层的物理坐标系设计，再到上层的 PBR 渲染，每个模块都经过了精心设计，展现了解决复杂 3D 引擎开发问题的能力。
