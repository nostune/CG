#include "Scene.h"
#include "components/TransformComponent.h"

namespace outer_wilds {

Scene::Scene(const std::string& name) : m_Name(name) {}

Scene::~Scene() {}

Entity Scene::CreateEntity(const std::string& name) {
    Entity entity = m_Registry.create();
    // Note: TransformComponent is not automatically added anymore
    // Entities that need transforms should add it explicitly
    return entity;
}

void Scene::DestroyEntity(Entity entity) {
    m_Registry.destroy(entity);
}

} // namespace outer_wilds
