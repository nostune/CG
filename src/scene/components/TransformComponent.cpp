#include "TransformComponent.h"

namespace outer_wilds {

DirectX::XMMATRIX TransformComponent::GetWorldMatrix() const {
    // Create transformation matrices
    DirectX::XMMATRIX translation = DirectX::XMMatrixTranslation(position.x, position.y, position.z);
    DirectX::XMMATRIX rotationMatrix = DirectX::XMMatrixRotationQuaternion(
        DirectX::XMLoadFloat4(&rotation)
    );
    DirectX::XMMATRIX scaling = DirectX::XMMatrixScaling(scale.x, scale.y, scale.z);

    // Combine transformations: Scale * Rotation * Translation
    return scaling * rotationMatrix * translation;
}

} // namespace outer_wilds