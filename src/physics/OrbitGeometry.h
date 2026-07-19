#pragma once

#include <DirectXMath.h>

namespace outer_wilds {

class OrbitGeometry {
public:
    static DirectX::XMFLOAT3 CalculatePosition(
        const DirectX::XMFLOAT3& center,
        float radius,
        float angle,
        const DirectX::XMFLOAT3& normal,
        float inclination);
};

} // namespace outer_wilds
