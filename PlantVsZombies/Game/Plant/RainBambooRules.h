#pragma once
#include <algorithm>
#include <array>

/** 穿雨竹实体、弹丸和数值推演共用的雨势攻击规则。 */
namespace RainBambooRules {
inline constexpr std::array<float,4> Intervals{5.0f,4.0f,3.5f,2.5f}; // 无雨至大雨的基础发射间隔，游戏秒
inline constexpr std::array<int,5> Damage{350,300,250,200,150}; // 同根竹矛每命中一个新目标，伤害递减50点
inline constexpr float Speed = 520; // 竹矛平射速度，像素/游戏秒
inline constexpr int Health = 300; // 本体生命
inline constexpr int IceCost = 25; // 冰块经济地图种植费用
inline float Interval(int rain) { return Intervals[std::clamp(rain,0,3)]; }
}
