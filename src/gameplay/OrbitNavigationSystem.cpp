#include "OrbitNavigationSystem.h"

#include "ObjectiveSystem.h"
#include "components/OrbitNavigationComponent.h"
#include "components/SolarMapState.h"
#include "components/SpacecraftComponent.h"
#include "../core/DebugManager.h"
#include "../input/InputManager.h"
#include "../physics/GravityEvaluator.h"
#include "../physics/OrbitGeometry.h"
#include "../physics/components/GravitySourceComponent.h"
#include "../physics/components/OrbitComponent.h"
#include "../physics/components/RigidBodyComponent.h"
#include "../physics/components/SectorComponent.h"
#include "../scene/Scene.h"
#include "../scene/components/TransformComponent.h"
#include <algorithm>
#include <cmath>

namespace outer_wilds {

using namespace components;

namespace {

constexpr float orbitClearance = 4.0f;
constexpr int predictionSteps = 480;

float Length(const DirectX::XMFLOAT3& value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

float Dot(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

DirectX::XMFLOAT3 Cross(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

DirectX::XMFLOAT3 Normalize(const DirectX::XMFLOAT3& value) {
    const float length = Length(value);
    if (length <= 0.0001f) return {1.0f, 0.0f, 0.0f};
    return {value.x / length, value.y / length, value.z / length};
}

} // namespace

void OrbitNavigationSystem::Initialize() {
    DebugManager::GetInstance().Info("OrbitNavigation", "Initialized");
}

void OrbitNavigationSystem::Initialize(std::shared_ptr<Scene> scene) {
    m_Scene = scene;
    Initialize();
}

void OrbitNavigationSystem::Update(float, entt::registry& registry) {
    auto spacecraft = registry.view<SpacecraftComponent, RigidBodyComponent, InSectorComponent>();
    for (const auto entity : spacecraft) {
        if (!registry.all_of<OrbitNavigationComponent>(entity)) {
            registry.emplace<OrbitNavigationComponent>(entity);
        }
        UpdateSpacecraft(registry, entity);
    }
    UpdateSolarMapState(registry);
}

void OrbitNavigationSystem::PrePhysicsUpdate(entt::registry& registry) {
    const auto* mapView = registry.ctx().find<SolarMapViewState>();
    const bool keyboardRequested = (!mapView || !mapView->requestedOpen) &&
        InputManager::GetInstance().IsKeyHeld('C');

    auto spacecraftView = registry.view<SpacecraftComponent, RigidBodyComponent, InSectorComponent>();
    for (const auto entity : spacecraftView) {
        auto& navigation = registry.get_or_emplace<OrbitNavigationComponent>(entity);
        navigation.circularizeActive = false;
        const bool requested = keyboardRequested || navigation.automatedCircularizeRequest;
        if (!requested ||
            spacecraftView.get<SpacecraftComponent>(entity).currentState != SpacecraftComponent::State::PILOTED) {
            continue;
        }

        const auto& rigidBody = spacecraftView.get<RigidBodyComponent>(entity);
        const auto& inSector = spacecraftView.get<InSectorComponent>(entity);
        auto* actor = rigidBody.physxActor
            ? rigidBody.physxActor->is<physx::PxRigidDynamic>()
            : nullptr;
        const auto* source = registry.try_get<GravitySourceComponent>(inSector.sector);
        if (!actor || !source) continue;

        const physx::PxTransform pose = actor->getGlobalPose();
        physx::PxVec3 radial = pose.p;
        const float distance = radial.magnitude();
        if (distance <= source->radius + orbitClearance) continue;
        radial /= distance;

        const auto gravity = GravityEvaluator::EvaluateLocal(
            *source, {pose.p.x, pose.p.y, pose.p.z});
        const float targetSpeed = std::sqrt((std::max)(0.0f, distance * gravity.strength));
        if (targetSpeed <= 0.01f) continue;

        const physx::PxVec3 velocity = actor->getLinearVelocity();
        physx::PxVec3 tangent = velocity - radial * velocity.dot(radial);
        if (tangent.magnitudeSquared() < 0.25f) {
            const physx::PxVec3 shipForward = pose.q.rotate(physx::PxVec3(0.0f, 0.0f, 1.0f));
            tangent = shipForward - radial * shipForward.dot(radial);
        }
        if (tangent.magnitudeSquared() < 0.01f) {
            const physx::PxVec3 reference = std::abs(radial.y) < 0.9f
                ? physx::PxVec3(0.0f, 1.0f, 0.0f)
                : physx::PxVec3(1.0f, 0.0f, 0.0f);
            tangent = reference.cross(radial);
        }
        tangent.normalize();

        const physx::PxVec3 targetVelocity = tangent * targetSpeed;
        physx::PxVec3 acceleration = (targetVelocity - velocity) * 3.0f;
        constexpr float maxAcceleration = 15.0f;
        const float magnitude = acceleration.magnitude();
        if (magnitude > maxAcceleration) acceleration *= maxAcceleration / magnitude;

        actor->wakeUp();
        actor->addForce(acceleration, physx::PxForceMode::eACCELERATION);
        navigation.circularizeActive = true;
        navigation.circularOrbitSpeed = targetSpeed;
    }
}

void OrbitNavigationSystem::UpdateSpacecraft(entt::registry& registry, entt::entity entity) {
    auto& spacecraft = registry.get<SpacecraftComponent>(entity);
    auto& rigidBody = registry.get<RigidBodyComponent>(entity);
    auto& inSector = registry.get<InSectorComponent>(entity);
    auto& navigation = registry.get<OrbitNavigationComponent>(entity);
    auto* actor = rigidBody.physxActor ? rigidBody.physxActor->is<physx::PxRigidDynamic>() : nullptr;
    auto* source = registry.try_get<GravitySourceComponent>(inSector.sector);
    auto* sector = registry.try_get<SectorComponent>(inSector.sector);
    if (!actor || !source || !sector) {
        navigation.stableOrbit = false;
        navigation.predictedTrajectory.clear();
        return;
    }

    if (navigation.objective == entt::null || !registry.valid(navigation.objective)) {
        navigation.objective = ObjectiveSystem::CreateObjective(
            registry,
            "flight.establish_orbit",
            "Establish a stable orbit",
            "Complete one revolution around any celestial body.",
            DirectX::XM_2PI);
        ObjectiveSystem::Activate(registry, navigation.objective);
    }

    if (navigation.centralBody != inSector.sector) {
        navigation.centralBody = inSector.sector;
        navigation.accumulatedAngle = 0.0f;
        navigation.hasPreviousRadial = false;
        if (auto* objective = registry.try_get<ObjectiveComponent>(navigation.objective)) {
            objective->target = inSector.sector;
            if (objective->status == ObjectiveStatus::Active) objective->progress = 0.0f;
        }
    }

    const auto pose = actor->getGlobalPose();
    const auto velocityPx = actor->getLinearVelocity();
    const DirectX::XMFLOAT3 position = {pose.p.x, pose.p.y, pose.p.z};
    const DirectX::XMFLOAT3 velocity = {velocityPx.x, velocityPx.y, velocityPx.z};
    const DirectX::XMFLOAT3 radial = Normalize(position);
    const float distance = Length(position);
    navigation.bodyRadius = source->radius;
    navigation.influenceRadius = source->GetInfluenceRadius();
    navigation.altitude = distance - source->radius;
    navigation.speed = Length(velocity);
    navigation.radialSpeed = Dot(velocity, radial);
    navigation.tangentialSpeed = std::sqrt((std::max)(
        0.0f, navigation.speed * navigation.speed - navigation.radialSpeed * navigation.radialSpeed));

    const auto currentGravity = GravityEvaluator::EvaluateLocal(*source, position);
    navigation.circularOrbitSpeed = std::sqrt((std::max)(
        0.0f, distance * currentGravity.strength));
    const float estimatedPeriod = navigation.circularOrbitSpeed > 0.01f
        ? DirectX::XM_2PI * distance / navigation.circularOrbitSpeed
        : 60.0f;
    const float predictionStep = std::clamp(estimatedPeriod / 400.0f, 0.05f, 0.75f);

    navigation.predictedTrajectory.clear();
    navigation.predictedTrajectory.reserve(predictionSteps + 1);
    navigation.predictedTrajectory.push_back(position);
    DirectX::XMFLOAT3 predictedPosition = position;
    DirectX::XMFLOAT3 predictedVelocity = velocity;
    float minDistance = distance;
    float maxDistance = distance;
    navigation.predictedImpact = false;
    navigation.predictedEscape = false;

    for (int step = 0; step < predictionSteps; ++step) {
        const auto gravity = GravityEvaluator::EvaluateLocal(*source, predictedPosition);
        predictedVelocity.x += gravity.acceleration.x * predictionStep;
        predictedVelocity.y += gravity.acceleration.y * predictionStep;
        predictedVelocity.z += gravity.acceleration.z * predictionStep;
        predictedPosition.x += predictedVelocity.x * predictionStep;
        predictedPosition.y += predictedVelocity.y * predictionStep;
        predictedPosition.z += predictedVelocity.z * predictionStep;
        const float predictedDistance = Length(predictedPosition);
        minDistance = (std::min)(minDistance, predictedDistance);
        maxDistance = (std::max)(maxDistance, predictedDistance);
        navigation.predictedTrajectory.push_back(predictedPosition);
        if (predictedDistance <= source->radius + 1.0f) {
            navigation.predictedImpact = true;
            break;
        }
        if (predictedDistance >= source->GetInfluenceRadius()) {
            navigation.predictedEscape = true;
            break;
        }
    }

    navigation.predictedPeriapsisAltitude = minDistance - source->radius;
    navigation.predictedApoapsisAltitude = maxDistance - source->radius;
    navigation.stableOrbit =
        spacecraft.currentState == SpacecraftComponent::State::PILOTED &&
        navigation.altitude >= orbitClearance &&
        navigation.tangentialSpeed >= 1.0f &&
        !navigation.predictedImpact && !navigation.predictedEscape;

    if (navigation.hasPreviousRadial && navigation.stableOrbit) {
        const float cosine = std::clamp(Dot(navigation.previousRadial, radial), -1.0f, 1.0f);
        const float sine = Length(Cross(navigation.previousRadial, radial));
        navigation.accumulatedAngle += std::atan2(sine, cosine);
        ObjectiveSystem::SetProgress(registry, navigation.objective, navigation.accumulatedAngle);
    }
    navigation.previousRadial = radial;
    navigation.hasPreviousRadial = true;

    auto& diagnostics = DebugManager::GetInstance();
    diagnostics.SetMetric("Orbit altitude", navigation.altitude, "m");
    diagnostics.SetMetric("Orbit radial speed", navigation.radialSpeed, "m/s");
    diagnostics.SetMetric("Orbit tangential speed", navigation.tangentialSpeed, "m/s");
    diagnostics.SetMetric("Orbit circular speed", navigation.circularOrbitSpeed, "m/s");
    diagnostics.SetMetric("Orbit prediction horizon", predictionStep * predictionSteps, "s");
    diagnostics.SetMetric("Orbit circularize active", navigation.circularizeActive ? 1.0 : 0.0);
    diagnostics.SetMetric("Orbit periapsis", navigation.predictedPeriapsisAltitude, "m");
    diagnostics.SetMetric("Orbit apoapsis", navigation.predictedApoapsisAltitude, "m");
    diagnostics.SetMetric("Orbit stable", navigation.stableOrbit ? 1.0 : 0.0);
}

void OrbitNavigationSystem::UpdateSolarMapState(entt::registry& registry) {
    if (!registry.ctx().contains<SolarMapState>()) {
        registry.ctx().emplace<SolarMapState>();
    }
    auto& map = registry.ctx().get<SolarMapState>();
    map = {};

    entt::entity currentSector = entt::null;
    entt::entity navigationEntity = entt::null;
    auto spacecraftView = registry.view<SpacecraftComponent, OrbitNavigationComponent, InSectorComponent>();
    for (const auto entity : spacecraftView) {
        navigationEntity = entity;
        if (spacecraftView.get<SpacecraftComponent>(entity).currentState == SpacecraftComponent::State::PILOTED) {
            break;
        }
    }

    if (navigationEntity != entt::null) {
        const auto& inSector = spacecraftView.get<InSectorComponent>(navigationEntity);
        const auto& navigation = spacecraftView.get<OrbitNavigationComponent>(navigationEntity);
        currentSector = inSector.sector;
        if (const auto* sector = registry.try_get<SectorComponent>(currentSector)) {
            map.predictedSpacecraftPath.owner = navigationEntity;
            map.predictedSpacecraftPath.dashed = true;
            map.predictedSpacecraftPath.points.reserve(navigation.predictedTrajectory.size());
            const auto rotation = DirectX::XMLoadFloat4(&sector->worldRotation);
            const auto center = DirectX::XMLoadFloat3(&sector->worldPosition);
            for (const auto& localPoint : navigation.predictedTrajectory) {
                const auto worldPoint = DirectX::XMVectorAdd(
                    DirectX::XMVector3Rotate(DirectX::XMLoadFloat3(&localPoint), rotation), center);
                DirectX::XMFLOAT3 storedWorldPoint;
                DirectX::XMStoreFloat3(&storedWorldPoint, worldPoint);
                map.predictedSpacecraftPath.points.push_back(storedWorldPoint);
            }
            if (!map.predictedSpacecraftPath.points.empty()) {
                map.spacecraftPosition = map.predictedSpacecraftPath.points.front();
                map.hasSpacecraft = true;
            }
            map.predictedImpact = navigation.predictedImpact;
            map.predictedEscape = navigation.predictedEscape;
            map.stableOrbit = navigation.stableOrbit;
        }
    }

    auto bodyView = registry.view<SectorComponent, TransformComponent>();
    for (const auto entity : bodyView) {
        const auto& sector = bodyView.get<SectorComponent>(entity);
        const auto& transform = bodyView.get<TransformComponent>(entity);
        map.bodies.push_back({entity, sector.name, transform.position, sector.planetRadius, entity == currentSector});
    }

    auto orbitView = registry.view<OrbitComponent>();
    for (const auto entity : orbitView) {
        const auto& orbit = orbitView.get<OrbitComponent>(entity);
        if (!orbit.orbitEnabled || orbit.orbitRadius <= 0.0f) continue;

        DirectX::XMFLOAT3 center = orbit.orbitCenter;
        if (orbit.orbitParent != entt::null) {
            if (const auto* parent = registry.try_get<TransformComponent>(orbit.orbitParent)) {
                center = parent->position;
            }
        }

        SolarMapPath path;
        path.owner = entity;
        constexpr int orbitSegments = 96;
        path.points.reserve(orbitSegments + 1);
        for (int index = 0; index <= orbitSegments; ++index) {
            const float angle = DirectX::XM_2PI * static_cast<float>(index) / static_cast<float>(orbitSegments);
            path.points.push_back(OrbitGeometry::CalculatePosition(
                center, orbit.orbitRadius, angle, orbit.orbitNormal, orbit.orbitInclination));
        }
        map.bodyOrbits.push_back(std::move(path));
    }
}

} // namespace outer_wilds
