/**
 * OrbitSystem.cpp
 * 
 * 轨道系统实现 - 处理天体公转和自转
 */

#include "OrbitSystem.h"
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
        float angularVelocity = orbit.GetOrbitalAngularVelocity();
        orbit.orbitAngle += angularVelocity * deltaTime;
        
        // 保持角度在 [0, 2π) 范围
        while (orbit.orbitAngle >= DirectX::XM_2PI) {
            orbit.orbitAngle -= DirectX::XM_2PI;
        }
        while (orbit.orbitAngle < 0.0f) {
            orbit.orbitAngle += DirectX::XM_2PI;
        }
        
        // 获取轨道中心（如果有父实体，使用父实体位置）
        DirectX::XMFLOAT3 center = orbit.orbitCenter;
        if (orbit.orbitParent != entt::null && registry.valid(orbit.orbitParent)) {
            auto* parentTransform = registry.try_get<TransformComponent>(orbit.orbitParent);
            if (parentTransform) {
                center = parentTransform->position;
            }
        }
        
        // 计算新的轨道位置
        DirectX::XMFLOAT3 newPosition = CalculateOrbitPosition(
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
}

void OrbitSystem::UpdateRotations(float deltaTime, entt::registry& registry) {
    auto view = registry.view<OrbitComponent>();
    
    for (auto entity : view) {
        auto& orbit = view.get<OrbitComponent>(entity);
        
        if (!orbit.rotationEnabled) continue;
        if (orbit.rotationPeriod <= 0.0f) continue;
        
        // 更新自转角度
        float angularVelocity = orbit.GetRotationAngularVelocity();
        orbit.rotationAngle += angularVelocity * deltaTime;
        
        // 保持角度在 [0, 2π) 范围
        while (orbit.rotationAngle >= DirectX::XM_2PI) {
            orbit.rotationAngle -= DirectX::XM_2PI;
        }
        
        // 更新 SectorComponent 的旋转（如果存在）
        auto* sector = registry.try_get<SectorComponent>(entity);
        if (sector) {
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

DirectX::XMFLOAT3 OrbitSystem::CalculateOrbitPosition(
    const DirectX::XMFLOAT3& center,
    float radius,
    float angle,
    const DirectX::XMFLOAT3& normal,
    float inclination
) {
    // 基本圆形轨道（在XZ平面上）
    float x = radius * std::cos(angle);
    float z = radius * std::sin(angle);
    float y = 0.0f;
    
    // 如果有倾角，应用倾斜
    if (std::abs(inclination) > 0.001f) {
        // 先在XZ平面计算，然后绕X轴旋转倾角
        float cosInc = std::cos(inclination);
        float sinInc = std::sin(inclination);
        float newY = z * sinInc;
        float newZ = z * cosInc;
        y = newY;
        z = newZ;
    }
    
    // 如果轨道平面法线不是Y轴，需要旋转整个轨道平面
    DirectX::XMFLOAT3 defaultUp = { 0.0f, 1.0f, 0.0f };
    DirectX::XMVECTOR defaultUpVec = DirectX::XMLoadFloat3(&defaultUp);
    DirectX::XMVECTOR normalVec = DirectX::XMLoadFloat3(&normal);
    normalVec = DirectX::XMVector3Normalize(normalVec);
    
    // 检查是否需要旋转（法线不是Y轴）
    DirectX::XMVECTOR dot = DirectX::XMVector3Dot(defaultUpVec, normalVec);
    float dotValue;
    DirectX::XMStoreFloat(&dotValue, dot);
    
    if (std::abs(dotValue) < 0.999f) {
        // 计算从Y轴到目标法线的旋转四元数
        DirectX::XMVECTOR rotAxis = DirectX::XMVector3Cross(defaultUpVec, normalVec);
        rotAxis = DirectX::XMVector3Normalize(rotAxis);
        float rotAngle = std::acos(std::clamp(dotValue, -1.0f, 1.0f));
        DirectX::XMVECTOR rotQuat = DirectX::XMQuaternionRotationAxis(rotAxis, rotAngle);
        
        // 旋转轨道位置
        DirectX::XMFLOAT3 pos = { x, y, z };
        DirectX::XMVECTOR posVec = DirectX::XMLoadFloat3(&pos);
        posVec = DirectX::XMVector3Rotate(posVec, rotQuat);
        DirectX::XMStoreFloat3(&pos, posVec);
        x = pos.x;
        y = pos.y;
        z = pos.z;
    }
    
    // 加上轨道中心
    return {
        center.x + x,
        center.y + y,
        center.z + z
    };
}

} // namespace outer_wilds
