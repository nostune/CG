# 添加新系统开发指南

以"交互系统(InteractionSystem)"为例,展示完整的开发流程。

## 1. 定义Component (20%)

在`src/gameplay/components/`下创建组件头文件:

```cpp
// InteractableComponent.h
struct InteractableComponent {
    std::string interactionType = "pickup";  // pickup, examine, trigger
    float interactionRange = 2.0f;           // 交互范围
    bool isInteractable = true;
    std::function<void(entt::entity)> onInteract;  // 回调函数
};

// InteractionStateComponent.h
struct InteractionStateComponent {
    entt::entity lastInteractedWith = entt::null;
    bool interactionInProgress = false;
    float interactionProgress = 0.0f;  // 0-1,用于长按交互
    float requiredHoldTime = 0.5f;     // 所需按住时间(秒)
};
```

**关键点**: 组件只存数据,不含逻辑。使用`std::function`存储回调而非虚函数。

## 2. 实现System

在`src/gameplay/`或`src/gameplay/systems/`创建系统:

```cpp
// InteractionSystem.h
class InteractionSystem : public System {
public:
    void Update(float deltaTime, entt::registry& registry) override {
        // 查找所有可交互对象和玩家
        auto view = registry.view<PlayerInputComponent, InteractionStateComponent, TransformComponent>();
        
        for (auto entity : view) {
            auto& input = view.get<PlayerInputComponent>(entity);
            auto& state = view.get<InteractionStateComponent>(entity);
            
            // 检测交互范围内的物体
            if (input.interactPressed) {
                FindNearbyInteractables(entity, registry);
            }
            
            // 更新交互进度
            if (state.interactionInProgress) {
                state.interactionProgress += deltaTime / state.requiredHoldTime;
                if (state.interactionProgress >= 1.0f) {
                    ExecuteInteraction(state.lastInteractedWith, registry);
                }
            }
        }
    }

private:
    void FindNearbyInteractables(entt::entity player, entt::registry& registry);
    void ExecuteInteraction(entt::entity target, entt::registry& registry);
};
```

## 3. 注册到Engine

在`src/core/Engine.h`添加成员:
```cpp
std::shared_ptr<InteractionSystem> m_InteractionSystem;
```

在`src/core/Engine.cpp`的`Initialize()`中注册(顺序很重要):
```cpp
// PlayerSystem之后,负责处理输入
m_InteractionSystem = AddSystem<InteractionSystem>();
```

**系统执行顺序参考**:
```
InputManager → PlayerSystem → InteractionSystem → PhysicsSystem → RenderSystem
```

## 4. 场景初始化

在`src/main.cpp`的场景加载部分给物体添加组件:
```cpp
auto interactable = registry.emplace<InteractableComponent>(itemEntity);
interactable.interactionType = "pickup";
interactable.onInteract = [](entt::entity item) {
    // 拾取逻辑
};

registry.emplace<InteractionStateComponent>(playerEntity);
```

## 5. 更新CMakeLists.txt (可选)

如果创建了新目录,添加到CMakeLists.txt:
```cmake
set(GAMEPLAY_SOURCES
    src/gameplay/PlayerSystem.cpp
    src/gameplay/systems/InteractionSystem.cpp  # 新增
)
```

## 6. 编译和测试

```powershell
cmake --build build --config Debug
```

## 最小改动清单

| 文件位置 | 操作 | 优先级 |
|---------|------|--------|
| `src/gameplay/components/` | 新建Component | 必须 |
| `src/gameplay/` | 新建System类 | 必须 |
| `src/core/Engine.cpp/h` | 注册System | 必须 |
| `src/main.cpp` | 初始化组件 | 必须 |
| `CMakeLists.txt` | 添加源文件 | 可选* |
| `src/core/ECS.h` | 引入头文件 | 可选** |

\* 如果System.cpp很大,否则可做header-only
\** 如果需要全局访问该System

## 最佳实践

1. **Component设计**: 数据最小化,使用简单类型。复杂逻辑放System。
2. **System职责单一**: InteractionSystem只处理交互逻辑,不处理物理。
3. **回调vs虚函数**: 使用`std::function`允许脚本绑定,比虚函数灵活。
4. **避免系统间耦合**: 通过Component通信,不直接调用其他System。
5. **Debug输出**: 在System的Update()中添加条件日志,便于诊断。

