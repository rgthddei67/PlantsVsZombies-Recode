#pragma once
#include <algorithm>
#include <array>

/** 穿雨竹实体、弹丸和数值推演共用的雨势攻击规则。 */
namespace RainBambooRules {
inline constexpr std::array<float,4> Intervals{6.5f,5.2f,4.0f,2.8f}; // 无雨至大雨的基础发射间隔，游戏秒
inline constexpr std::array<int,5> Damage{500,400,300,200,100}; // 同根竹矛按不同目标命中次序衰减
inline constexpr float Speed = 520; // 竹矛平射速度，像素/游戏秒
inline constexpr int Health = 300; // 本体生命
inline constexpr int IceCost = 25; // 冰块经济地图种植费用
inline float Interval(int rain) { return Intervals[std::clamp(rain,0,3)]; }
}
