#pragma once

#include <entt/entt.hpp>
#include <string>

namespace outer_wilds::components {

enum class ObjectiveStatus {
    Inactive,
    Active,
    Completed,
    Failed
};

struct ObjectiveComponent {
    std::string id;
    std::string title;
    std::string description;
    ObjectiveStatus status = ObjectiveStatus::Inactive;
    ObjectiveStatus observedStatus = ObjectiveStatus::Inactive;
    float progress = 0.0f;
    float requiredProgress = 1.0f;
    entt::entity target = entt::null;
    int displayOrder = 0;
    bool showInHud = true;
};

} // namespace outer_wilds::components
