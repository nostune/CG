#pragma once
#include <entt/entt.hpp>

namespace outer_wilds {
namespace components {

/**
 * Component for entities that should follow a parent entity's transform.
 * Used for multi-material models where each submesh is a separate entity
 * but needs to move with the parent.
 */
struct ChildEntityComponent {
    entt::entity parent = entt::null;
    
    // Optional local offset from parent (not used yet, reserved for future)
    DirectX::XMFLOAT3 localOffset = { 0.0f, 0.0f, 0.0f };
};

} // namespace components
} // namespace outer_wilds
