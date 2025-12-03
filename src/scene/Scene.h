#pragma once
#include "../core/ECS.h"
#include <string>
#include <entt/entt.hpp>

namespace outer_wilds {

class Scene {
public:
    Scene(const std::string& name = "Default Scene");
    ~Scene();

    Entity CreateEntity(const std::string& name = "Entity");
    void DestroyEntity(Entity entity);
    
    entt::registry& GetRegistry() { return m_Registry; }
    const entt::registry& GetRegistry() const { return m_Registry; }

    const std::string& GetName() const { return m_Name; }

private:
    std::string m_Name;
    entt::registry m_Registry;
};

} // namespace outer_wilds
