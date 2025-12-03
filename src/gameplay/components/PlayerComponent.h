#pragma once
#include "../../core/ECS.h"

namespace outer_wilds {

struct PlayerComponent : public Component {
    float moveSpeed = 5.0f;
    float sprintMultiplier = 1.5f;
    float jumpForce = 5.0f;
    
    float lookSensitivity = 1.0f;
    bool isGrounded = false;
    
    Entity cameraEntity = entt::null;
};

} // namespace outer_wilds
