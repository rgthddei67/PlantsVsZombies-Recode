#pragma once

#include <algorithm>
#include <cmath>

enum class PlanternGear : int {
	OFF = 0,
	LOW = 1,
	MEDIUM = 2,
	HIGH = 3,
};

namespace PlanternRules {
	constexpr int kPlanternLowBackRadius = 1;              // 一档向房屋侧照亮的格数
	constexpr int kPlanternLowFrontRadius = 2;             // 一档向僵尸来向照亮的格数
	constexpr int kPlanternLowVerticalRadius = 1;          // 一档向上下照亮的格数
	constexpr int kPlanternMediumBaseRadiusX = 3;          // 二档原有主体向左右照亮的格数
	constexpr int kPlanternMediumVerticalRadius = 2;       // 二档向上下照亮的格数
	constexpr int kPlanternMediumManhattanLimit = 4;       // 二档主体裁去远角时允许的最大横纵格距和
	constexpr int kPlanternMediumFrontExtension = 4;       // 二档向僵尸来向新增的最远列格距
	constexpr int kPlanternMediumFrontHalfHeight = 1;      // 二档新增前沿列向上下延伸的格数
	constexpr int kPlanternHighBaseRadiusX = 4;            // 三档原有主体向左右照亮的格数
	constexpr int kPlanternHighVerticalRadius = 3;         // 三档向上下照亮的格数
	constexpr int kPlanternHighManhattanLimit = 6;         // 三档主体裁去远角时允许的最大横纵格距和
	constexpr int kPlanternHighFrontExtension = 5;         // 三档向僵尸来向新增的最远列格距
	constexpr int kPlanternHighFrontHalfHeight = 2;        // 三档新增前沿列向上下延伸的格数
	constexpr float kPlanternHighEdgeIllumination = 0.72f; // 三档最外圈保留的照明比例
	constexpr float kLowBurnRate = 0.5f;     // 一档每游戏秒消耗的雾火
	constexpr float kMediumBurnRate = 1.1f;  // 二档每游戏秒消耗的雾火
	constexpr float kHighEarlyBurnRate = 2.1f; // 首波三档每游戏秒消耗的雾火
	constexpr float kHighLateBurnRate = 4.0f;  // 最终波三档每游戏秒消耗的雾火
	constexpr float FuelCapacity = 100.0f; // 灯芯最大燃料
	constexpr float InitialFuel = 25.0f; // 新种路灯花初始燃料
	constexpr float FuelFlightSeconds = 0.62f; // 雾火从死亡位置飞抵灯芯的游戏秒

/** 气象站没有最终波；第31波起采用后期III挡消耗，其他场地保留平滑进度。 */
inline float Scarcity(bool station,int wave,float ordinary) { return station ? (wave>30 ? 1.0f : 0.0f) : ordinary; }

/** 当前挡位按本关雾火紧缩进度采用的每游戏秒耗油。 */
inline float BurnRate(PlanternGear gear,float scarcity) {
	switch(gear) {
	case PlanternGear::LOW: return kLowBurnRate;
	case PlanternGear::MEDIUM: return kMediumBurnRate;
	case PlanternGear::HIGH: return kHighEarlyBurnRate+(kHighLateBurnRate-kHighEarlyBurnRate)*std::clamp(scarcity,0.0f,1.0f);
	default: return 0;
	}
}

/** 以光源格位为原点的实际逐格照明比例，III 挡最外圈保留薄雾。 */
inline float Illumination(PlanternGear gear,int relativeRow,int relativeColumn) {
	const int relativeX = relativeColumn;
	const int relativeY = relativeRow;
	const int dx = std::abs(relativeX);
	const int dy = std::abs(relativeY);
	switch (gear) {
	case PlanternGear::OFF:
		return 0.0f;
	case PlanternGear::LOW:
		return relativeX >= -kPlanternLowBackRadius
			&& relativeX <= kPlanternLowFrontRadius
			&& dy <= kPlanternLowVerticalRadius
			? 1.0f : 0.0f;
	case PlanternGear::MEDIUM:
		return (dx <= kPlanternMediumBaseRadiusX
				&& dy <= kPlanternMediumVerticalRadius
				&& dx + dy <= kPlanternMediumManhattanLimit)
			|| (relativeX == kPlanternMediumFrontExtension
				&& dy <= kPlanternMediumFrontHalfHeight)
			? 1.0f : 0.0f;
	case PlanternGear::HIGH: {
		const auto isInsideHighShape = [](int x, int y) {
			const int shapeDx = std::abs(x);
			const int shapeDy = std::abs(y);
			return (shapeDx <= kPlanternHighBaseRadiusX
					&& shapeDy <= kPlanternHighVerticalRadius
					&& shapeDx + shapeDy <= kPlanternHighManhattanLimit)
				|| (x == kPlanternHighFrontExtension
					&& shapeDy <= kPlanternHighFrontHalfHeight);
		};
		if (!isInsideHighShape(relativeX, relativeY)) return 0.0f;
		// 由四邻域实时识别扩展后轮廓，只有真正最外圈保留薄雾。
		const bool isOuterEdge = !isInsideHighShape(relativeX - 1, relativeY)
			|| !isInsideHighShape(relativeX + 1, relativeY)
			|| !isInsideHighShape(relativeX, relativeY - 1)
			|| !isInsideHighShape(relativeX, relativeY + 1);
		return isOuterEdge ? kPlanternHighEdgeIllumination : 1.0f;
	}
	}
	return 0.0f;
}
}
