#pragma once
#include <PxPhysicsAPI.h>

namespace outer_wilds {
namespace components {

struct CharacterControllerComponent {
    physx::PxController* controller = nullptr;
    
    // Movement settings
    float moveSpeed = 5.0f;
    float jumpSpeed = 8.0f;
    float gravity = -20.0f;
    
    // Current state
    float verticalVelocity = 0.0f;
    bool isGrounded = false;
    
    // Input
    float forwardInput = 0.0f;  // -1 to 1 (S to W)
    float rightInput = 0.0f;    // -1 to 1 (A to D)
    bool wantsToJump = false;
};

} // namespace components
} // namespace outer_wilds
