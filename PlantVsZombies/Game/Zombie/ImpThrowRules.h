#pragma once

#include <algorithm>
#include <cmath>

/** 正式小鬼飞行与数值推演共用的逻辑参数，不涉及视觉锚点修正。 */
namespace ImpThrowRules {
inline constexpr int Health = 270; // 小鬼本体生命
inline constexpr float HorizontalSpeed = 300; // 飞行水平速度，像素/游戏秒
inline constexpr float Gravity = 500; // 飞行重力，像素/游戏秒平方
inline constexpr float OriginalAltitude = 88; // 确定既有飞行时长的原版高度，像素
inline constexpr float ReleaseOffsetX = 133; // 脱手逻辑点相对投手原点的水平距离，像素
inline constexpr float MinimumDistance = 40; // 巨人超过半场锚点才可开始投掷的距离，像素

/** 正式飞行时长；抬高显示起点不改变此时长。 */
inline float FlightSeconds(float distance) {
	const float vertical = .5f * (std::max(0.0f,distance)/HorizontalSpeed) * Gravity;
	return (vertical+std::sqrt(vertical*vertical+2*Gravity*OriginalAltitude))/Gravity;
}
}
