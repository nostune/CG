#pragma once

#include <cstdint>
#include <entt/entt.hpp>
#include <string>
#include <vector>

namespace outer_wilds::components {

enum class GamePhase {
    Exploring,
    ObjectiveComplete
};

enum class ObjectiveEventType {
    Activated,
    Completed,
    Failed
};

struct ObjectiveEvent {
    std::uint64_t sequence = 0;
    entt::entity objective = entt::null;
    ObjectiveEventType type = ObjectiveEventType::Activated;
    std::string objectiveId;
};

// Scene-wide state belongs in the registry context, not on a synthetic entity.
struct GameState {
    GamePhase phase = GamePhase::Exploring;
    entt::entity activeObjective = entt::null;
    std::uint32_t completedObjectiveCount = 0;
    std::uint64_t nextEventSequence = 1;
    std::vector<ObjectiveEvent> objectiveEvents;
};

} // namespace outer_wilds::components
