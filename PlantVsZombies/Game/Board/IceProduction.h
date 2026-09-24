#pragma once

#include <algorithm>
#include <cmath>

/** 制冰工的正式生产与指挥官预测共用同一离散产量规则。 */
namespace IceProduction {
inline constexpr float Interval = 4.0f; // 每批生产间隔，游戏秒，成长不缩短间隔
inline constexpr float InitialYield = 3.0f; // 初次每批产量，冰块
inline constexpr float YieldGrowth = 1.5f; // 每次成功生产后下批产量倍率
inline constexpr float MaximumYield = 10.0f; // 单只每批产量上限，冰块；限制受保护工人的长期滚雪球
inline constexpr int WorkerCost = 24; // 指挥官购买一名制冰工的冰价
inline constexpr int WorkerHealth = 500; // 制冰工本体生命，不附加护甲
inline constexpr float MintInterval = 20.0f; // 薄荷每批制冰间隔，游戏秒
inline constexpr int MintYield = 3; // 薄荷每批冰块，依靠长期投资回本
inline constexpr float MintSunInterval = 40.0f; // 非冷藏站每颗普通阳光间隔，游戏秒

/** 预测给定有效存活时间内的整数收入，不更改正式生产状态。 */
inline int Forecast(float remaining, float nextYield, float seconds)
{
	if (!std::isfinite(seconds) || seconds <= 0.0f) return 0;
	seconds = (std::min)(seconds, 120.0f);
	int ice = 0;
	for (float t = (std::max)(0.0f, remaining); t <= seconds; t += Interval) {
		ice += static_cast<int>(nextYield);
		nextYield = (std::min)(MaximumYield, nextYield * YieldGrowth);
	}
	return ice;
}
}
