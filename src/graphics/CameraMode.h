#pragma once

namespace outer_wilds {

/// <summary>
/// 相机模式枚举
/// </summary>
enum class CameraMode {
    Player,     // 玩家第一人称视角（受物理和重力影响）
    Spacecraft, // 飞船第一人称视角
    Free        // 自由视角（调试/浏览模式，无物理约束）
};

} // namespace outer_wilds
