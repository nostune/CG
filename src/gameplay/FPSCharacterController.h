#pragma once
#include "../core/ECS.h"
#include "../scene/components/TransformComponent.h"
#include "../physics/components/RigidBodyComponent.h"
#include "../physics/components/ColliderComponent.h"
#include <DirectXMath.h>

namespace OWC {

class FPSCharacterController {
public:
    FPSCharacterController() = default;
    ~FPSCharacterController() = default;

    void Initialize(EntityID entity);
    void Update(float deltaTime);
    
    void SetMoveInput(const DirectX::XMFLOAT2& input);
    void SetLookInput(const DirectX::XMFLOAT2& input);
    void Jump();
    
    float moveSpeed = 5.0f;
    float jumpForce = 5.0f;
    float lookSensitivity = 1.0f;

private:
    EntityID m_Entity = 0;
    DirectX::XMFLOAT2 m_MoveInput = { 0.0f, 0.0f };
    DirectX::XMFLOAT2 m_LookInput = { 0.0f, 0.0f };
    bool m_WantsToJump = false;
};

} // namespace OWC
