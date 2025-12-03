#pragma once
#include "../../core/ECS.h"
#include <DirectXMath.h>

namespace outer_wilds {

struct PlayerInputComponent : public Component {
    // Movement input
    DirectX::XMFLOAT2 moveInput = { 0.0f, 0.0f };
    
    // Look input
    DirectX::XMFLOAT2 lookInput = { 0.0f, 0.0f };
    
    // Action buttons
    bool jumpPressed = false;
    bool jumpHeld = false;
    bool sprintHeld = false;
    bool crouchHeld = false;
    bool interactPressed = false;
    
    // Camera control
    bool mouseLookEnabled = false;
    bool toggleMouseLookPressed = false;
};

} // namespace outer_wilds
