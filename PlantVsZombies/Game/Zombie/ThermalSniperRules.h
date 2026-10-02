#pragma once

/** 正式实体与数值预测共用的热感狙击时序和弹道参数。 */
namespace ThermalSniperRules {
inline constexpr float Reload = 1.5f; // 独立装填周期，游戏秒；鼓舞不缩短
inline constexpr float Aim = .35f; // 落种到出膛的预警前摇，游戏秒
inline constexpr float PulseSpeed = 1800; // 热脉冲水平飞行速度，像素/游戏秒
inline constexpr float MuzzleOffset = 52; // 枪口沿面朝方向偏移，像素
}
