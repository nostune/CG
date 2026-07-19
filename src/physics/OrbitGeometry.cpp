#include "OrbitGeometry.h"
#include <algorithm>
#include <cmath>

namespace outer_wilds {

DirectX::XMFLOAT3 OrbitGeometry::CalculatePosition(
    const DirectX::XMFLOAT3& center,
    float radius,
    float angle,
    const DirectX::XMFLOAT3& normal,
    float inclination) {
    float x = radius * std::cos(angle);
    float z = radius * std::sin(angle);
    float y = 0.0f;

    if (std::abs(inclination) > 0.001f) {
        const float cosInclination = std::cos(inclination);
        const float sinInclination = std::sin(inclination);
        y = z * sinInclination;
        z *= cosInclination;
    }

    const auto defaultUp = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    auto normalVector = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&normal));
    const float dot = DirectX::XMVectorGetX(DirectX::XMVector3Dot(defaultUp, normalVector));
    if (std::abs(dot) < 0.999f) {
        auto rotationAxis = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(defaultUp, normalVector));
        const auto rotation = DirectX::XMQuaternionRotationAxis(
            rotationAxis, std::acos(std::clamp(dot, -1.0f, 1.0f)));
        DirectX::XMFLOAT3 rotated;
        DirectX::XMStoreFloat3(
            &rotated,
            DirectX::XMVector3Rotate(DirectX::XMVectorSet(x, y, z, 0.0f), rotation));
        x = rotated.x;
        y = rotated.y;
        z = rotated.z;
    }

    return {center.x + x, center.y + y, center.z + z};
}

} // namespace outer_wilds
