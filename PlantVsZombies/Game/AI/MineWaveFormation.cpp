#include "MineWaveFormation.h"
#include <algorithm>
#include <array>
#include <limits>

namespace MineWaveFormation {
namespace {
	constexpr int kEmptyLaneBonus = 12; // 空进攻方向优先分，避免随机漏路后固定两口就能覆盖
	constexpr int kPrimaryLaneBonus = 14; // 集中攻势的主路优先分；已有单位越多，继续集中收益越低
	constexpr int kSupportFrontBonus = 30; // 支援优先跟随承伤前排，不把稀有辅助单独丢到一条空路
	constexpr int kLaneLoadCost = 5; // 同一路每只已有普通单位的拥挤成本
}

float RankOffset(int role)
{
	return role == FRONT ? 0.0f : role == SUPPORT ? 1.2f : 0.45f;
}

void Arrange(std::vector<Unit>& units, const std::array<int,5>& rowOrder, bool concentrate)
{
	if (units.size() < 3) return;
	// 前排先定进攻方向，再让辅助贴着队伍跟进；稳定排序保留同角色原抽取顺序。
	std::stable_sort(units.begin(),units.end(),[](const Unit& a, const Unit& b) {
		const auto order = [](int role) { return role == FRONT ? 0 : role == SUPPORT ? 3 : role == EXCAVATOR ? 2 : 1; };
		return order(a.role) < order(b.role);
	});
	std::array<int,5> counts{}, fronts{}, supports{};
	int primary = -1;
	for (auto& unit : units) {
		int best = std::numeric_limits<int>::min(), chosen = unit.row;
		for (int row : rowOrder) {
			if (!(unit.legalRows & (1 << row))) continue;
			int score = -kLaneLoadCost*counts[row] + (counts[row] == 0 ? kEmptyLaneBonus : 0);
			if (concentrate && row == primary) score += kPrimaryLaneBonus;
			if (unit.role == SUPPORT) score += kSupportFrontBonus*std::min(2,fronts[row])-kSupportFrontBonus*supports[row];
			// 工兵从侧路接近施工点，保留与正面队伍在共享矿道会合的可能。
			if (unit.role == EXCAVATOR && row == primary) score -= kPrimaryLaneBonus;
			if (score > best) { best = score; chosen = row; }
		}
		unit.row = chosen;
		if (primary < 0) primary = chosen;
		++counts[chosen];
		if (unit.role == FRONT) ++fronts[chosen];
		if (unit.role == SUPPORT) ++supports[chosen];
	}
}
}
