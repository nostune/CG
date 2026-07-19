#include "GravityEvaluator.h"
#include <cmath>

namespace outer_wilds {

GravitySample GravityEvaluator::EvaluateLocal(
    const components::GravitySourceComponent& source,
    const DirectX::XMFLOAT3& localPosition,
    float gravityScale) {
    GravitySample sample;
    sample.distance = std::sqrt(
        localPosition.x * localPosition.x +
        localPosition.y * localPosition.y +
        localPosition.z * localPosition.z);
    if (sample.distance <= 0.01f) return sample;

    sample.strength = source.CalculateGravityStrength(sample.distance) * gravityScale;
    const float inverseDistance = 1.0f / sample.distance;
    sample.acceleration = {
        -localPosition.x * inverseDistance * sample.strength,
        -localPosition.y * inverseDistance * sample.strength,
        -localPosition.z * inverseDistance * sample.strength
    };
    return sample;
}

} // namespace outer_wilds
