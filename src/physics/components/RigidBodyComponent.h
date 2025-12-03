#pragma once
#include "../../core/ECS.h"
#include <DirectXMath.h>
#include <PxPhysicsAPI.h>

namespace outer_wilds {

struct RigidBodyComponent : public Component {
    float mass = 1.0f;
    float drag = 0.0f;
    float angularDrag = 0.05f;
    
    DirectX::XMFLOAT3 velocity = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 angularVelocity = { 0.0f, 0.0f, 0.0f };
    
    bool useGravity = true;
    bool isKinematic = false;
    
    // Constraints
    bool freezePositionX = false;
    bool freezePositionY = false;
    bool freezePositionZ = false;
    bool freezeRotationX = false;
    bool freezeRotationY = false;
    bool freezeRotationZ = false;
    
    // PhysX actor handle
    physx::PxRigidActor* physxActor = nullptr;
};

} // namespace outer_wilds
