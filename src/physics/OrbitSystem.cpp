/**
 * OrbitSystem.cpp
 * 
 * 轨道系统实现 - 处理天体公转和自转
 */

#include "OrbitSystem.h"
#include "OrbitGeometry.h"
#include "components/OrbitComponent.h"
#include "components/SectorComponent.h"
#include "../scene/components/TransformComponent.h"
#include "../core/DebugManager.h"
#include <cmath>

namespace outer_wilds {

using namespace components;

void OrbitSystem::Initialize() {
    DebugManager::GetInstance().Log("OrbitSystem", "Initialized");
}

void OrbitSystem::Initialize(std::shared_ptr<Scene> scene) {
    m_Scene = scene;
    Initialize();
}

void OrbitSystem::Update(float deltaTime, entt::registry& registry) {
    UpdateOrbits(deltaTime, registry);
    UpdateRotations(deltaTime, registry);
}

void OrbitSystem::Shutdown() {
    DebugManager::GetInstance().Log("OrbitSystem", "Shutdown");
}

void OrbitSystem::UpdateOrbits(float deltaTime, entt::registry& registry) {
    auto view = registry.view<OrbitComponent>();
    
    for (auto entity : view) {
        auto& orbit = view.get<OrbitComponent>(entity);
        
        if (!orbit.orbitEnabled) continue;
        if (orbit.orbitPeriod <= 0.0f) continue;
        
        // 更新轨道角度
        const float angularVelocity = orbit.GetOrbitalAngularVelocity();
        if (!orbit.orbitPhaseInitialized) {
            orbit.orbitPhase = static_cast<double>(orbit.orbitAngle);
            orbit.orbitPhaseInitialized = true;
        }
        orbit.orbitPhase = std::fmod(
            orbit.orbitPhase + static_cast<double>(angularVelocity) * deltaTime,
            static_cast<double>(DirectX::XM_2PI));
        if (orbit.orbitPhase < 0.0) orbit.orbitPhase += DirectX::XM_2PI;
        orbit.orbitAngle = static_cast<float>(orbit.orbitPhase);
        
        // 保持角度在 [0, 2π) 范围
        
        // 获取轨道中心（如果有父实体，使用父实体位置）
        DirectX::XMFLOAT3 center = orbit.orbitCenter;
        if (orbit.orbitParent != entt::null && registry.valid(orbit.orbitParent)) {
            auto* parentTransform = registry.try_get<TransformComponent>(orbit.orbitParent);
            if (parentTransform) {
                center = parentTransform->position;
            }
        }
        
        // 计算新的轨道位置
        DirectX::XMFLOAT3 newPosition = OrbitGeometry::CalculatePosition(
            center,
            orbit.orbitRadius,
            orbit.orbitAngle,
            orbit.orbitNormal,
            orbit.orbitInclination
        );
        
        // 计算轨道速度（切向速度 = 角速度 × 轨道半径）
        // 速度方向垂直于位置向量，在轨道平面内
        DirectX::XMFLOAT3 orbitVelocity = { 0.0f, 0.0f, 0.0f };
        if (orbit.orbitEnabled && orbit.orbitPeriod > 0.0f) {
            // 轨道线速度大小 = 2πr / T = 角速度 × 半径
            float linearSpeed = angularVelocity * orbit.orbitRadius;
            // 速度方向：垂直于径向，在轨道平面内
            // 径向 = (pos - center) 的归一化
            float dx = newPosition.x - center.x;
            float dz = newPosition.z - center.z;
            float r = std::sqrt(dx*dx + dz*dz);
            if (r > 0.001f) {
                // 切向 = 垂直于径向，逆时针方向（默认轨道方向）
                orbitVelocity.x = -dz / r * linearSpeed;
                orbitVelocity.y = 0.0f;  // 假设水平轨道
                orbitVelocity.z = dx / r * linearSpeed;
            }
        }
        // 更新 SectorComponent（如果存在）
        auto* sector = registry.try_get<SectorComponent>(entity);
        if (sector) {
            sector->worldPosition = newPosition;
            sector->worldVelocity = orbitVelocity;  // 更新扇区速度
        }
        
        // 更新 TransformComponent（如果存在）
        auto* transform = registry.try_get<TransformComponent>(entity);
        if (transform) {
            transform->position = newPosition;
        }
    }

    // Compose child world velocities only after every body's own orbital
    // velocity has been updated. This keeps the result independent of EnTT
    // iteration order (for example Moon velocity includes Earth velocity).
    for (const auto entity : view) {
        const auto& orbit = view.get<OrbitComponent>(entity);
        if (orbit.orbitParent == entt::null) continue;
        auto* sector = registry.try_get<SectorComponent>(entity);
        const auto* parentSector = registry.try_get<SectorComponent>(orbit.orbitParent);
        if (!sector || !parentSector) continue;
        sector->worldVelocity.x += parentSector->worldVelocity.x;
        sector->worldVelocity.y += parentSector->worldVelocity.y;
        sector->worldVelocity.z += parentSector->worldVelocity.z;
    }
}

void OrbitSystem::UpdateRotations(float deltaTime, entt::registry& registry) {
    auto view = registry.view<OrbitComponent>();
    
    for (auto entity : view) {
        auto& orbit = view.get<OrbitComponent>(entity);
        
        if (!orbit.rotationEnabled) continue;
        if (orbit.rotationPeriod <= 0.0f) continue;
        
        // 更新自转角度
        const float angularVelocity = orbit.GetRotationAngularVelocity();
        if (!orbit.rotationPhaseInitialized) {
            orbit.rotationPhase = static_cast<double>(orbit.rotationAngle);
            orbit.rotationPhaseInitialized = true;
        }
        orbit.rotationPhase = std::fmod(
            orbit.rotationPhase + static_cast<double>(angularVelocity) * deltaTime,
            static_cast<double>(DirectX::XM_2PI));
        if (orbit.rotationPhase < 0.0) orbit.rotationPhase += DirectX::XM_2PI;
        orbit.rotationAngle = static_cast<float>(orbit.rotationPhase);
        
        // 保持角度在 [0, 2π) 范围
        
        // 更新 SectorComponent 的旋转（如果存在）
        auto* sector = registry.try_get<SectorComponent>(entity);
        if (sector && sector->rotatesWithBody) {
            // 围绕自转轴旋转
            DirectX::XMVECTOR axis = DirectX::XMLoadFloat3(&orbit.rotationAxis);
            axis = DirectX::XMVector3Normalize(axis);
            DirectX::XMVECTOR rotQuat = DirectX::XMQuaternionRotationAxis(axis, orbit.rotationAngle);
            DirectX::XMStoreFloat4(&sector->worldRotation, rotQuat);
        }
        
        // 更新 TransformComponent（如果存在）
        auto* transform = registry.try_get<TransformComponent>(entity);
        if (transform) {
            DirectX::XMVECTOR axis = DirectX::XMLoadFloat3(&orbit.rotationAxis);
            axis = DirectX::XMVector3Normalize(axis);
            DirectX::XMVECTOR rotQuat = DirectX::XMQuaternionRotationAxis(axis, orbit.rotationAngle);
            DirectX::XMStoreFloat4(&transform->rotation, rotQuat);
        }
    }
}

} // namespace outer_wilds
