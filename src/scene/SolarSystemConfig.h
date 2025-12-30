/**
 * SolarSystemConfig.h
 * 
 * 微缩太阳系配置 - 定义行星大小、轨道参数等
 * 
 * 设计原则：
 * 1. 微缩宇宙 - 玩家可以在行星表面行走，也可以在太空中观察整个太阳系
 * 2. 地球半径 32m（直径 64m）作为基准
 * 3. 轨道间距适中，便于观察和旅行
 * 4. 公转周期适当加快，让玩家能看到行星运动
 */

#pragma once
#include <DirectXMath.h>
#include <string>
#include <vector>

namespace outer_wilds {

/**
 * 行星配置数据
 */
struct PlanetConfig {
    // 基本信息
    std::string name;           // 行星名称
    std::string modelPath;      // 模型路径
    std::string texturePath;    // 纹理路径（如果模型不包含）
    
    // 尺寸（单位：米）
    float radius;               // 目标半径
    float surfaceGravity;       // 表面重力 (m/s²)
    float atmosphereHeight;     // 大气层高度（0 = 无大气）
    
    // 轨道参数
    float orbitRadius;          // 轨道半径（与太阳的距离）
    float orbitPeriod;          // 公转周期（秒）
    float orbitInclination;     // 轨道倾角（弧度）
    float initialAngle;         // 初始轨道角度（弧度）
    
    // 自转参数
    float rotationPeriod;       // 自转周期（秒，0 = 不自转）
    float axialTilt;            // 轴向倾斜（弧度）
    
    // 渲染属性
    bool hasRings;              // 是否有星环
    bool isEmissive;            // 是否发光（太阳）
    bool isTidalLocked;         // 是否潮汐锁定
    
    // 物理属性
    bool isGravitySource;       // 是否是重力源
    bool hasCollision;          // 是否有碰撞体
};

/**
 * 卫星配置数据
 */
struct MoonConfig {
    std::string name;
    std::string modelPath;
    std::string texturePath;
    
    float radius;
    float orbitRadius;          // 与主行星的距离
    float orbitPeriod;
    float rotationPeriod;       // 通常与公转周期相同（潮汐锁定）
    
    std::string parentPlanet;   // 父行星名称
};

/**
 * 微缩太阳系配置
 * 
 * 比例说明：
 * - 真实地球半径：6,371 km
 * - 游戏地球半径：32 m
 * - 缩放比例：约 1:200,000
 * 
 * 但轨道距离不能按同比例缩放（否则太远），采用对数缩放
 */
class SolarSystemConfig {
public:
    // ========================================
    // 基准参数
    // ========================================
    static constexpr float EARTH_RADIUS = 64.0f;          // 地球半径（游戏基准，与手动创建一致）
    static constexpr float EARTH_GRAVITY = 9.8f;          // 地球表面重力
    static constexpr float BASE_ORBIT_UNIT = 250.0f;      // 轨道基本单位（250m，进一步增大间距）
    static constexpr float BASE_ORBIT_PERIOD = 600.0f;    // 基础公转周期（600秒=10分钟，增大2倍）
    
    // ========================================
    // 太阳参数
    // ========================================
    static constexpr float SUN_RADIUS = 200.0f;           // 太阳半径（增大到200m，更有存在感）
    
    // ========================================
    // 行星相对大小（相对于地球）
    // 真实比例做了调整，让小行星不会太小
    // ========================================
    static constexpr float MERCURY_SCALE = 0.4f;          // 真实0.38
    static constexpr float VENUS_SCALE = 0.95f;           // 真实0.95
    static constexpr float EARTH_SCALE = 1.0f;
    static constexpr float MARS_SCALE = 0.53f;            // 真实0.53
    static constexpr float JUPITER_SCALE = 3.0f;          // 真实11.2，但太大会遮挡
    static constexpr float SATURN_SCALE = 2.5f;           // 真实9.4
    static constexpr float URANUS_SCALE = 1.5f;           // 真实4.0
    static constexpr float NEPTUNE_SCALE = 1.4f;          // 真实3.9
    static constexpr float MOON_SCALE = 0.18f;            // 从0.27减小到0.18，更小的月球
    
    // ========================================
    // 轨道距离（单位：BASE_ORBIT_UNIT）
    // 使用紧凑但有层次的间距
    // ========================================
    static constexpr float MERCURY_ORBIT = 2.0f;          // 200m
    static constexpr float VENUS_ORBIT = 3.5f;            // 350m
    static constexpr float EARTH_ORBIT = 5.0f;            // 500m
    static constexpr float MARS_ORBIT = 7.0f;             // 700m
    static constexpr float JUPITER_ORBIT = 12.0f;         // 1200m
    static constexpr float SATURN_ORBIT = 18.0f;          // 1800m
    static constexpr float URANUS_ORBIT = 25.0f;          // 2500m
    static constexpr float NEPTUNE_ORBIT = 32.0f;         // 3200m
    static constexpr float MOON_ORBIT = 0.8f;             // 200m（相对地球，从1.5减小到0.8）
    
    // ========================================
    // 公转周期倍数（相对于基础周期）
    // 内侧行星转得快，外侧行星转得慢
    // ========================================
    static constexpr float MERCURY_PERIOD_MULT = 0.5f;    // 300秒
    static constexpr float VENUS_PERIOD_MULT = 0.8f;      // 480秒
    static constexpr float EARTH_PERIOD_MULT = 1.0f;      // 600秒
    static constexpr float MARS_PERIOD_MULT = 1.5f;       // 900秒
    static constexpr float JUPITER_PERIOD_MULT = 3.0f;    // 1800秒
    static constexpr float SATURN_PERIOD_MULT = 5.0f;     // 3000秒
    static constexpr float URANUS_PERIOD_MULT = 8.0f;     // 4800秒
    static constexpr float NEPTUNE_PERIOD_MULT = 12.0f;   // 7200秒
    static constexpr float MOON_PERIOD_MULT = 0.5f;       // 300秒（围绕地球）
    
    // ========================================
    // 静态配置生成方法
    // ========================================
    
    /**
     * 获取太阳配置
     * 太阳作为整个太阳系的"默认扇区"，所有行星都在其影响范围内
     */
    static PlanetConfig GetSunConfig() {
        return {
            "Sun",
            "assets/models/sun/sun_final.glb",
            "",
            SUN_RADIUS,
            0.0f,                           // 无表面重力（不可着陆）
            0.0f,                           // 无大气
            0.0f,                           // 轨道中心
            0.0f,
            0.0f,
            0.0f,
            120.0f,                         // 自转周期120秒（加倍）
            0.0f,
            false,                          // 无星环
            true,                           // 发光
            false,
            true,                           // 【改为重力源】作为默认太空扇区
            false                           // 无碰撞（太阳不能着陆）
        };
    }
    
    /**
     * 获取水星配置
     */
    static PlanetConfig GetMercuryConfig() {
        return {
            "Mercury",
            "assets/models/mercury/Mercury.obj",
            "assets/models/mercury/Mercury.jpg",
            EARTH_RADIUS * MERCURY_SCALE,
            EARTH_GRAVITY * 0.38f,
            0.0f,                           // 无大气
            BASE_ORBIT_UNIT * MERCURY_ORBIT,
            BASE_ORBIT_PERIOD * MERCURY_PERIOD_MULT,
            0.0f,
            0.0f,
            300.0f,                          // 自转周期（从150秒增加到300秒）
            0.0f,
            false, false, false,
            true, true  // 启用碰撞体
        };
    }
    
    /**
     * 获取金星配置
     */
    static PlanetConfig GetVenusConfig() {
        return {
            "Venus",
            "assets/models/venus/Venus.obj",
            "assets/models/venus/Venus.jpg",
            EARTH_RADIUS * VENUS_SCALE,
            EARTH_GRAVITY * 0.9f,
            EARTH_RADIUS * VENUS_SCALE * 0.3f,  // 厚大气层
            BASE_ORBIT_UNIT * VENUS_ORBIT,
            BASE_ORBIT_PERIOD * VENUS_PERIOD_MULT,
            DirectX::XMConvertToRadians(3.4f),  // 轻微倾斜
            DirectX::XM_PIDIV4,                  // 初始角度45度
            400.0f,                              // 自转周期（从200秒增加到400秒）
            DirectX::XMConvertToRadians(177.0f), // 逆行自转
            false, false, false,
            true, true  // 启用碰撞体
        };
    }
    
    /**
     * 获取地球配置
     * 使用 GLB 地球模型（带建筑的复杂模型）
     */
    static PlanetConfig GetEarthConfig() {
        return {
            "Earth",
            "assets/BlendObj/earth/earth_final.glb",  // GLB 地球模型
            "",  // GLB 自带纹理
            EARTH_RADIUS,
            EARTH_GRAVITY,
            EARTH_RADIUS * 0.4f,             // 使用一定高度的重力影响范围（仅用于重力衰减，可视大气仍可按需关闭）
            BASE_ORBIT_UNIT * EARTH_ORBIT,
            BASE_ORBIT_PERIOD * EARTH_PERIOD_MULT,
            0.0f,
            DirectX::XM_PI,                   // 初始角度180度（对面）
            300.0f,                            // 自转（从150秒增加到300秒）
            DirectX::XMConvertToRadians(23.5f), // 轴向倾斜
            false, false, false,
            true, true
        };
    }
    
    /**
     * 获取火星配置
     */
    static PlanetConfig GetMarsConfig() {
        return {
            "Mars",
            "assets/models/mars/Mars.obj",
            "assets/models/mars/InShot_20250116_121259944.jpg",
            EARTH_RADIUS * MARS_SCALE,
            EARTH_GRAVITY * 0.38f,
            EARTH_RADIUS * MARS_SCALE * 0.1f,  // 稀薄大气
            BASE_ORBIT_UNIT * MARS_ORBIT,
            BASE_ORBIT_PERIOD * MARS_PERIOD_MULT,
            DirectX::XMConvertToRadians(1.9f),
            DirectX::XM_PIDIV2,                // 初始角度90度
            350.0f,                            // 自转周期（从175秒增加到350秒）
            DirectX::XMConvertToRadians(25.2f),
            false, false, false,
            true, true  // 启用碰撞体
        };
    }
    
    /**
     * 获取木星配置
     */
    static PlanetConfig GetJupiterConfig() {
        return {
            "Jupiter",
            "assets/models/jupiter/Jupiter.obj",
            "assets/models/jupiter/Jupiter.jpg",
            EARTH_RADIUS * JUPITER_SCALE,
            EARTH_GRAVITY * 2.36f,
            EARTH_RADIUS * JUPITER_SCALE * 0.5f,  // 厚大气层
            BASE_ORBIT_UNIT * JUPITER_ORBIT,
            BASE_ORBIT_PERIOD * JUPITER_PERIOD_MULT,
            DirectX::XMConvertToRadians(1.3f),
            DirectX::XM_PI * 0.7f,
            200.0f,                              // 自转周期（从100秒增加到200秒）
            DirectX::XMConvertToRadians(3.1f),
            false, false, false,
            true, true  // 启用碰撞体
        };
    }
    
    /**
     * 获取土星配置
     */
    static PlanetConfig GetSaturnConfig() {
        return {
            "Saturn",
            "assets/models/saturn/saturn_planet.glb",
            "",                              // GLB包含纹理
            EARTH_RADIUS * SATURN_SCALE,
            EARTH_GRAVITY * 0.92f,
            EARTH_RADIUS * SATURN_SCALE * 0.4f,
            BASE_ORBIT_UNIT * SATURN_ORBIT,
            BASE_ORBIT_PERIOD * SATURN_PERIOD_MULT,
            DirectX::XMConvertToRadians(2.5f),
            DirectX::XM_PI * 1.3f,
            250.0f,                          // 自转周期（从125秒增加到250秒）
            DirectX::XMConvertToRadians(26.7f),
            true,                            // 【有星环】
            false, false,
            true, true  // 启用碰撞体
        };
    }
    
    /**
     * 获取天王星配置（如果没有模型则返回空）
     * 注：当前assets中没有天王星模型
     */
    static PlanetConfig GetUranusConfig() {
        return {
            "Uranus",
            "",                              // TODO: 添加天王星模型
            "",
            EARTH_RADIUS * URANUS_SCALE,
            EARTH_GRAVITY * 0.89f,
            EARTH_RADIUS * URANUS_SCALE * 0.3f,
            BASE_ORBIT_UNIT * URANUS_ORBIT,
            BASE_ORBIT_PERIOD * URANUS_PERIOD_MULT,
            DirectX::XMConvertToRadians(0.8f),
            DirectX::XM_PI * 1.6f,
            350.0f,                          // 自转周期（从175秒增加到350秒）
            DirectX::XMConvertToRadians(97.8f), // 极端轴向倾斜
            true,                            // 有星环（薄）
            false, false,
            true, true  // 启用碰撞体
        };
    }
    
    /**
     * 获取海王星配置
     */
    static PlanetConfig GetNeptuneConfig() {
        return {
            "Neptune",
            "assets/models/neptune/Neptune.obj",
            "assets/models/neptune/Neptune.jpg",
            EARTH_RADIUS * NEPTUNE_SCALE,
            EARTH_GRAVITY * 1.12f,
            EARTH_RADIUS * NEPTUNE_SCALE * 0.35f,
            BASE_ORBIT_UNIT * NEPTUNE_ORBIT,
            BASE_ORBIT_PERIOD * NEPTUNE_PERIOD_MULT,
            DirectX::XMConvertToRadians(1.8f),
            DirectX::XM_PI * 0.3f,
            300.0f,                          // 自转周期（从150秒增加到300秒）
            DirectX::XMConvertToRadians(28.3f),
            false, false, false,
            true, true  // 启用碰撞体
        };
    }
    
    /**
     * 获取月球配置
     */
    static MoonConfig GetMoonConfig() {
        return {
            "Moon",
            "assets/models/moon/lunar_artifacts_globe.glb",
            "",
            EARTH_RADIUS * MOON_SCALE,
            BASE_ORBIT_UNIT * MOON_ORBIT,
            BASE_ORBIT_PERIOD * MOON_PERIOD_MULT,
            BASE_ORBIT_PERIOD * MOON_PERIOD_MULT,  // 潮汐锁定
            "Earth"
        };
    }
    
    /**
     * 获取所有行星配置（按轨道顺序）
     */
    static std::vector<PlanetConfig> GetAllPlanets() {
        std::vector<PlanetConfig> planets;
        planets.push_back(GetSunConfig());
        planets.push_back(GetMercuryConfig());
        planets.push_back(GetVenusConfig());
        planets.push_back(GetEarthConfig());
        planets.push_back(GetMarsConfig());
        planets.push_back(GetJupiterConfig());
        planets.push_back(GetSaturnConfig());
        // 天王星暂时跳过（没有模型）
        planets.push_back(GetNeptuneConfig());
        return planets;
    }
    
    /**
     * 根据名称获取行星配置
     */
    static PlanetConfig GetPlanetByName(const std::string& name) {
        if (name == "Sun") return GetSunConfig();
        if (name == "Mercury") return GetMercuryConfig();
        if (name == "Venus") return GetVenusConfig();
        if (name == "Earth") return GetEarthConfig();
        if (name == "Mars") return GetMarsConfig();
        if (name == "Jupiter") return GetJupiterConfig();
        if (name == "Saturn") return GetSaturnConfig();
        if (name == "Uranus") return GetUranusConfig();
        if (name == "Neptune") return GetNeptuneConfig();
        return {};  // 空配置
    }
};

} // namespace outer_wilds
