/**
 * SolarSystemBuilder.h
 * 
 * 太阳系构建器 - 使用配置创建整个太阳系
 */

#pragma once
#include "SolarSystemConfig.h"
#include "SceneAssetLoader.h"
#include "Scene.h"
#include "components/TransformComponent.h"
#include "../physics/components/OrbitComponent.h"
#include "../physics/components/SectorComponent.h"
#include "../physics/components/GravitySourceComponent.h"
#include "../physics/PhysXManager.h"
#include <entt/entt.hpp>
#include <map>
#include <iostream>
#include <algorithm>
#include <cctype>

namespace outer_wilds {

/**
 * 太阳系构建结果
 */
struct SolarSystemEntities {
    entt::entity sun = entt::null;
    entt::entity mercury = entt::null;
    entt::entity venus = entt::null;
    entt::entity earth = entt::null;
    entt::entity mars = entt::null;
    entt::entity jupiter = entt::null;
    entt::entity saturn = entt::null;
    entt::entity uranus = entt::null;
    entt::entity neptune = entt::null;
    entt::entity moon = entt::null;
    
    // 名称到实体的映射
    std::map<std::string, entt::entity> byName;
    
    entt::entity GetByName(const std::string& name) const {
        auto it = byName.find(name);
        return (it != byName.end()) ? it->second : entt::null;
    }
};

/**
 * 太阳系构建器
 */
class SolarSystemBuilder {
public:
    /**
     * 构建完整的太阳系
     */
    static SolarSystemEntities Build(
        entt::registry& registry,
        std::shared_ptr<Scene> scene,
        ID3D11Device* device,
        const std::string& assetsBasePath
    ) {
        SolarSystemEntities result;
        auto& physxManager = PhysXManager::GetInstance();
        auto* pxPhysics = physxManager.GetPhysics();
        auto* pxScene = physxManager.GetScene();

        
        // 创建太阳
        result.sun = CreateSun(registry, scene, device, assetsBasePath);
        if (result.sun != entt::null) {
            result.byName["Sun"] = result.sun;
        }
        
        // 创建行星
        result.mercury = CreatePlanet(registry, scene, device, assetsBasePath, 
                                      SolarSystemConfig::GetMercuryConfig(), pxPhysics, pxScene);
        if (result.mercury != entt::null) result.byName["Mercury"] = result.mercury;
        
        result.venus = CreatePlanet(registry, scene, device, assetsBasePath,
                                    SolarSystemConfig::GetVenusConfig(), pxPhysics, pxScene);
        if (result.venus != entt::null) result.byName["Venus"] = result.venus;
        
        result.earth = CreatePlanet(registry, scene, device, assetsBasePath,
                                    SolarSystemConfig::GetEarthConfig(), pxPhysics, pxScene);
        if (result.earth != entt::null) result.byName["Earth"] = result.earth;
        
        result.mars = CreatePlanet(registry, scene, device, assetsBasePath,
                                   SolarSystemConfig::GetMarsConfig(), pxPhysics, pxScene);
        if (result.mars != entt::null) result.byName["Mars"] = result.mars;
        
        result.jupiter = CreatePlanet(registry, scene, device, assetsBasePath,
                                      SolarSystemConfig::GetJupiterConfig(), pxPhysics, pxScene);
        if (result.jupiter != entt::null) result.byName["Jupiter"] = result.jupiter;
        
        result.saturn = CreatePlanet(registry, scene, device, assetsBasePath,
                                     SolarSystemConfig::GetSaturnConfig(), pxPhysics, pxScene);
        if (result.saturn != entt::null) result.byName["Saturn"] = result.saturn;
        
        result.neptune = CreatePlanet(registry, scene, device, assetsBasePath,
                                      SolarSystemConfig::GetNeptuneConfig(), pxPhysics, pxScene);
        if (result.neptune != entt::null) result.byName["Neptune"] = result.neptune;
        
        // 创建月球（围绕地球）
        if (result.earth != entt::null) {
            result.moon = CreateMoon(registry, scene, device, assetsBasePath,
                                     SolarSystemConfig::GetMoonConfig(), result.earth);
            if (result.moon != entt::null) result.byName["Moon"] = result.moon;
        }

        
        return result;
    }
    
private:
    /**
     * 创建太阳
     * 太阳作为整个太阳系的"默认扇区"，优先级最低
     */
    static entt::entity CreateSun(
        entt::registry& registry,
        std::shared_ptr<Scene> scene,
        ID3D11Device* device,
        const std::string& basePath
    ) {
        auto config = SolarSystemConfig::GetSunConfig();
        std::string modelPath = basePath + "/" + config.modelPath;
        

        
        float actualRadius = 0.0f;
        auto entity = SceneAssetLoader::LoadModelWithRadius(
            registry, scene, device,
            modelPath,
            config.texturePath.empty() ? "" : basePath + "/" + config.texturePath,
            DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),  // 轨道中心
            config.radius,
            &actualRadius
        );
        
        if (entity != entt::null) {
            // 添加自转
            auto& orbit = registry.emplace<components::OrbitComponent>(entity);
            orbit.orbitEnabled = false;           // 太阳不公转
            orbit.rotationEnabled = true;         // 但会自转
            orbit.rotationPeriod = config.rotationPeriod;
            orbit.rotationAxis = { 0.0f, 1.0f, 0.0f };
            
            // 获取 PhysX 引擎
            auto& physxManager = PhysXManager::GetInstance();
            auto* pxPhysics = physxManager.GetPhysics();
            auto* pxScene = physxManager.GetScene();
            
            // 太阳作为默认"太空"扇区
            auto& sector = registry.emplace<components::SectorComponent>(entity);
            sector.name = "Sun (Space)";
            sector.worldPosition = { 0.0f, 0.0f, 0.0f };
            sector.worldRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
            sector.planetRadius = config.radius;
            // 太阳扇区覆盖整个太阳系（海王星轨道 32 * 250 = 8000m，再加一些余量）
            sector.influenceRadius = 15000.0f;
            sector.priority = -100;  // 最低优先级，只有不在任何行星扇区时才使用
            sector.isActive = true;
            
            // 太阳不需要碰撞体（不能着陆）
            sector.physxGround = nullptr;
            
            std::cout << "[Sun] Created as default space sector, influenceRadius=" << sector.influenceRadius 
                      << ", priority=" << sector.priority << std::endl;

        } else {
            std::cout << "[Sun] FAILED to load model: " << modelPath << std::endl;
        }
        
        return entity;
    }
    
    /**
     * 创建行星
     */
    static entt::entity CreatePlanet(
        entt::registry& registry,
        std::shared_ptr<Scene> scene,
        ID3D11Device* device,
        const std::string& basePath,
        const PlanetConfig& config,
        physx::PxPhysics* pxPhysics,
        physx::PxScene* pxScene
    ) {
        if (config.modelPath.empty()) {
            std::cout << "[" << config.name << "] Skipped (no model)" << std::endl;
            return entt::null;
        }
        
        std::string modelPath = basePath + "/" + config.modelPath;
        std::string texturePath = config.texturePath.empty() ? "" : basePath + "/" + config.texturePath;
        
        
        // 计算初始位置（基于轨道半径和初始角度）
        float x = config.orbitRadius * std::cos(config.initialAngle);
        float z = config.orbitRadius * std::sin(config.initialAngle);
        DirectX::XMFLOAT3 position = { x, 0.0f, z };

        // 检测文件扩展名，决定使用哪种加载方式
        std::string ext = modelPath.substr(modelPath.find_last_of('.') + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        float actualRadius = 0.0f;
        entt::entity entity = entt::null;
        
        // GLB/GLTF 使用多材质加载器
        if (ext == "glb" || ext == "gltf") {
            entity = SceneAssetLoader::LoadMultiMaterialModelWithRadius(
                registry, scene, device,
                modelPath,
                texturePath,
                position,
                config.radius,
                &actualRadius
            );
        } else {
            // OBJ/FBX 等使用普通加载器
            entity = SceneAssetLoader::LoadModelWithRadius(
                registry, scene, device,
                modelPath,
                texturePath,
                position,
                config.radius,
                &actualRadius
            );
        }
        
        if (entity == entt::null) {
            std::cout << "[" << config.name << "] FAILED to load model: " << modelPath << std::endl;
            return entt::null;
        }
        
        // 验证 TransformComponent 的位置
        auto* transform = registry.try_get<TransformComponent>(entity);
        if (transform) {
        }
        
        // 添加轨道组件
        auto& orbit = registry.emplace<components::OrbitComponent>(entity);
        orbit.orbitCenter = { 0.0f, 0.0f, 0.0f };      // 围绕太阳
        orbit.orbitRadius = config.orbitRadius;
        orbit.orbitPeriod = config.orbitPeriod;
        orbit.orbitAngle = config.initialAngle;
        orbit.orbitNormal = { 0.0f, 1.0f, 0.0f };
        orbit.orbitInclination = config.orbitInclination;
        orbit.orbitEnabled = true;
        orbit.rotationEnabled = config.rotationPeriod > 0;
        orbit.rotationPeriod = config.rotationPeriod;
        orbit.rotationAxis = { 
            std::sin(config.axialTilt), 
            std::cos(config.axialTilt), 
            0.0f 
        };
        
        // 如果是重力源，添加相关组件
        if (config.isGravitySource) {
            // SectorComponent
            auto& sector = registry.emplace<components::SectorComponent>(entity);
            sector.name = config.name;
            sector.worldPosition = position;
            sector.worldRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
            sector.planetRadius = config.radius;
            // 扇区影响半径：星球半径的 3 倍，确保从远处接近时能检测到
            // 这个范围足够大，让玩家在接近星球时就能切换扇区
            sector.influenceRadius = config.radius * 3.0f;
            sector.priority = 0;  // 普通行星优先级为0
            sector.isActive = true;
            
            // GravitySourceComponent
            auto& gravity = registry.emplace<components::GravitySourceComponent>(entity);
            gravity.radius = config.radius;
            gravity.surfaceGravity = config.surfaceGravity;
            // 重力影响范围也要足够大
            gravity.atmosphereHeight = config.radius * 2.0f;
            gravity.isActive = true;
            gravity.useRealisticGravity = false;
            
            // 如果需要碰撞体
            // 【重要】PhysX 碰撞体必须在扇区原点 (0,0,0)！
            // SectorPhysicsSystem 使用 InSectorComponent.localPosition（相对于扇区原点）计算物理
            // 扇区的 worldPosition 由 OrbitSystem 更新，用于坐标转换
            if (config.hasCollision && pxPhysics && pxScene) {
                physx::PxMaterial* material = pxPhysics->createMaterial(0.5f, 0.5f, 0.3f);
                // 【关键修复】碰撞体在原点，而不是世界位置！
                physx::PxTransform pose(physx::PxVec3(0.0f, 0.0f, 0.0f));
                physx::PxRigidStatic* actor = pxPhysics->createRigidStatic(pose);
                
                physx::PxShape* shape = physx::PxRigidActorExt::createExclusiveShape(
                    *actor,
                    physx::PxSphereGeometry(config.radius),
                    *material
                );
                shape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE, false);  // 默认禁用
                shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);   // 默认禁用，由 SectorPhysicsSystem 启用当前扇区
                
                pxScene->addActor(*actor);
                sector.physxGround = actor;
                
                std::cout << "[" << config.name << "] Created sector with radius=" << config.radius 
                          << ", influenceRadius=" << sector.influenceRadius << ", priority=" << sector.priority << std::endl;
            }
        }
        

        
        return entity;
    }
    
    /**
     * 创建卫星（围绕行星公转）
     * 卫星也有自己的扇区、重力和碰撞体
     */
    static entt::entity CreateMoon(
        entt::registry& registry,
        std::shared_ptr<Scene> scene,
        ID3D11Device* device,
        const std::string& basePath,
        const MoonConfig& config,
        entt::entity parentPlanet
    ) {
        std::string modelPath = basePath + "/" + config.modelPath;
        std::string texturePath = config.texturePath.empty() ? "" : basePath + "/" + config.texturePath;
        
        std::cout << "\n=== Creating " << config.name << " (orbiting " << config.parentPlanet << ") ===" << std::endl;
        
        // 获取父行星位置
        auto* parentTransform = registry.try_get<TransformComponent>(parentPlanet);
        if (!parentTransform) {
            return entt::null;
        }
        
        // 初始位置（在父行星旁边）
        DirectX::XMFLOAT3 position = {
            parentTransform->position.x + config.orbitRadius,
            parentTransform->position.y,
            parentTransform->position.z
        };
        
        float actualRadius = 0.0f;
        auto entity = SceneAssetLoader::LoadModelWithRadius(
            registry, scene, device,
            modelPath,
            texturePath,
            position,
            config.radius,
            &actualRadius
        );
        
        if (entity == entt::null) {
            
            return entt::null;
        }
        
        // 添加轨道组件（围绕父行星）
        auto& orbit = registry.emplace<components::OrbitComponent>(entity);
        orbit.orbitParent = parentPlanet;             // 【关键】围绕父行星
        orbit.orbitCenter = parentTransform->position;
        orbit.orbitRadius = config.orbitRadius;
        orbit.orbitPeriod = config.orbitPeriod;
        orbit.orbitAngle = 0.0f;
        orbit.orbitNormal = { 0.0f, 1.0f, 0.0f };
        orbit.orbitInclination = 0.1f;                // 轻微倾斜
        orbit.orbitEnabled = true;
        orbit.rotationEnabled = true;                 // 潮汐锁定
        orbit.rotationPeriod = config.rotationPeriod;
        
        // 获取 PhysX 引擎
        auto& physxManager = PhysXManager::GetInstance();
        auto* pxPhysics = physxManager.GetPhysics();
        auto* pxScene = physxManager.GetScene();
        
        // 添加 SectorComponent（月球有自己的扇区）
        auto& sector = registry.emplace<components::SectorComponent>(entity);
        sector.name = config.name;
        sector.worldPosition = position;
        sector.worldRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
        sector.planetRadius = config.radius;
        // 月球扇区影响半径：半径的2.5倍
        sector.influenceRadius = config.radius * 2.5f;
        // 【关键】月球优先级比地球高！这样在月球附近时会优先进入月球扇区
        sector.priority = 10;
        sector.parentSector = parentPlanet;
        sector.isActive = true;
        
        // 添加 GravitySourceComponent
        const float MOON_SURFACE_GRAVITY = 1.62f;  // 月球表面重力
        auto& gravity = registry.emplace<components::GravitySourceComponent>(entity);
        gravity.radius = config.radius;
        gravity.surfaceGravity = MOON_SURFACE_GRAVITY;
        gravity.atmosphereHeight = config.radius * 0.5f;  // 月球重力影响范围
        gravity.isActive = true;
        gravity.useRealisticGravity = false;
        
        // 创建 PhysX 碰撞体
        if (pxPhysics && pxScene) {
            physx::PxMaterial* material = pxPhysics->createMaterial(0.5f, 0.5f, 0.3f);
            // 碰撞体在扇区原点
            physx::PxTransform pose(physx::PxVec3(0.0f, 0.0f, 0.0f));
            physx::PxRigidStatic* actor = pxPhysics->createRigidStatic(pose);
            
            physx::PxShape* shape = physx::PxRigidActorExt::createExclusiveShape(
                *actor,
                physx::PxSphereGeometry(config.radius),
                *material
            );
            // 默认禁用，由 SectorPhysicsSystem 在切换到月球扇区时启用
            shape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE, false);
            shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
            
            pxScene->addActor(*actor);
            sector.physxGround = actor;
            
            std::cout << "[" << config.name << "] Created sector with radius=" << config.radius 
                      << ", influenceRadius=" << sector.influenceRadius << ", priority=" << sector.priority << std::endl;
        }

        
        return entity;
    }
};

} // namespace outer_wilds
