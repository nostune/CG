#include "ObjectiveSystem.h"

#include "components/GameState.h"
#include "../core/DebugManager.h"
#include "../scene/Scene.h"
#include <algorithm>
#include <limits>
#include <utility>

namespace outer_wilds {

namespace {

const char* StatusName(components::ObjectiveStatus status) {
    switch (status) {
        case components::ObjectiveStatus::Inactive: return "inactive";
        case components::ObjectiveStatus::Active: return "active";
        case components::ObjectiveStatus::Completed: return "completed";
        case components::ObjectiveStatus::Failed: return "failed";
    }
    return "unknown";
}

components::ObjectiveEventType EventTypeFor(components::ObjectiveStatus status) {
    switch (status) {
        case components::ObjectiveStatus::Completed: return components::ObjectiveEventType::Completed;
        case components::ObjectiveStatus::Failed: return components::ObjectiveEventType::Failed;
        default: return components::ObjectiveEventType::Activated;
    }
}

} // namespace

void ObjectiveSystem::Initialize() {
    DebugManager::GetInstance().Info("ObjectiveSystem", "Initialized");
}

void ObjectiveSystem::Initialize(std::shared_ptr<Scene> scene) {
    m_Scene = scene;
    if (scene) {
        EnsureGameState(scene->GetRegistry());
    }
    Initialize();
}

void ObjectiveSystem::Update(float, entt::registry& registry) {
    EnsureGameState(registry);
    auto& gameState = registry.ctx().get<components::GameState>();

    auto objectives = registry.view<components::ObjectiveComponent>();
    for (const auto entity : objectives) {
        auto& objective = objectives.get<components::ObjectiveComponent>(entity);
        objective.requiredProgress = (std::max)(objective.requiredProgress, 0.0001f);
        objective.progress = std::clamp(objective.progress, 0.0f, objective.requiredProgress);

        if (objective.status == components::ObjectiveStatus::Active &&
            objective.progress >= objective.requiredProgress) {
            objective.status = components::ObjectiveStatus::Completed;
        }

        if (objective.status != objective.observedStatus) {
            PublishTransition(registry, entity, objective);
        }
    }

    if (gameState.activeObjective != entt::null &&
        (!registry.valid(gameState.activeObjective) ||
         !registry.all_of<components::ObjectiveComponent>(gameState.activeObjective))) {
        gameState.activeObjective = entt::null;
    }

    if (gameState.activeObjective == entt::null) {
        entt::entity nextObjective = entt::null;
        int nextOrder = (std::numeric_limits<int>::max)();
        for (const auto entity : objectives) {
            const auto& objective = objectives.get<components::ObjectiveComponent>(entity);
            if (objective.status == components::ObjectiveStatus::Active && objective.displayOrder < nextOrder) {
                nextObjective = entity;
                nextOrder = objective.displayOrder;
            }
        }
        gameState.activeObjective = nextObjective;
    }

    gameState.phase = gameState.activeObjective == entt::null && gameState.completedObjectiveCount > 0
        ? components::GamePhase::ObjectiveComplete
        : components::GamePhase::Exploring;

    DebugManager::GetInstance().SetMetric(
        "Objectives active", gameState.activeObjective == entt::null ? 0.0 : 1.0);
    DebugManager::GetInstance().SetMetric(
        "Objectives completed", static_cast<double>(gameState.completedObjectiveCount));
}

entt::entity ObjectiveSystem::CreateObjective(
    entt::registry& registry,
    std::string id,
    std::string title,
    std::string description,
    float requiredProgress,
    entt::entity target) {
    EnsureGameState(registry);
    auto existingObjectives = registry.view<components::ObjectiveComponent>();
    for (const auto existingEntity : existingObjectives) {
        if (existingObjectives.get<components::ObjectiveComponent>(existingEntity).id == id) {
            return existingEntity;
        }
    }

    const auto entity = registry.create();
    auto& objective = registry.emplace<components::ObjectiveComponent>(entity);
    objective.id = std::move(id);
    objective.title = std::move(title);
    objective.description = std::move(description);
    objective.requiredProgress = (std::max)(requiredProgress, 0.0001f);
    objective.target = target;
    return entity;
}

void ObjectiveSystem::Activate(entt::registry& registry, entt::entity objective) {
    if (auto* component = registry.try_get<components::ObjectiveComponent>(objective)) {
        component->status = components::ObjectiveStatus::Active;
    }
}

void ObjectiveSystem::SetProgress(entt::registry& registry, entt::entity objective, float progress) {
    if (auto* component = registry.try_get<components::ObjectiveComponent>(objective)) {
        component->progress = progress;
    }
}

void ObjectiveSystem::Complete(entt::registry& registry, entt::entity objective) {
    if (auto* component = registry.try_get<components::ObjectiveComponent>(objective)) {
        component->progress = component->requiredProgress;
        component->status = components::ObjectiveStatus::Completed;
    }
}

void ObjectiveSystem::Fail(entt::registry& registry, entt::entity objective) {
    if (auto* component = registry.try_get<components::ObjectiveComponent>(objective)) {
        component->status = components::ObjectiveStatus::Failed;
    }
}

void ObjectiveSystem::EnsureGameState(entt::registry& registry) {
    if (!registry.ctx().contains<components::GameState>()) {
        registry.ctx().emplace<components::GameState>();
    }
}

void ObjectiveSystem::PublishTransition(
    entt::registry& registry,
    entt::entity entity,
    components::ObjectiveComponent& objective) {
    auto& gameState = registry.ctx().get<components::GameState>();
    const auto previousStatus = objective.observedStatus;
    objective.observedStatus = objective.status;

    if (objective.status == components::ObjectiveStatus::Active) {
        gameState.activeObjective = entity;
    } else if (gameState.activeObjective == entity) {
        gameState.activeObjective = entt::null;
    }

    if (objective.status == components::ObjectiveStatus::Completed &&
        previousStatus != components::ObjectiveStatus::Completed) {
        ++gameState.completedObjectiveCount;
    }

    if (objective.status != components::ObjectiveStatus::Inactive) {
        gameState.objectiveEvents.push_back({
            gameState.nextEventSequence++, entity, EventTypeFor(objective.status), objective.id});
        constexpr std::size_t maxBufferedEvents = 64;
        if (gameState.objectiveEvents.size() > maxBufferedEvents) {
            gameState.objectiveEvents.erase(gameState.objectiveEvents.begin());
        }
    }

    DebugManager::GetInstance().Info(
        "ObjectiveSystem", objective.id + " -> " + StatusName(objective.status));
}

} // namespace outer_wilds
