#include "MineGrid.h"
#include <algorithm>
#include <tuple>

namespace {
	constexpr std::array<std::array<int, 2>, 4> kDirections{{{0,-1},{-1,0},{1,0},{0,1}}}; // 选墙/行走并列按左、上、下、右
	constexpr int kRouteBenefitWeight = 3; // 每个入口节省一格的优先分；以三倍赶路速度折算施工路程成本
}

std::array<int, MineGrid::Count> MineGrid::WorkDistances(int start) const
{
	std::array<int, Count> result;
	result.fill(Unreachable);
	if (start < 0 || start >= Count || start % Columns < 2 || rock[start] || !connected[start]) return result;
	std::array<int, Count> queue{};
	int head = 0, tail = 0;
	queue[tail++] = start;
	result[start] = 0;
	while (head < tail) {
		const int cell = queue[head++];
		for (const auto& d : kDirections) {
			const int r = cell / Columns + d[0], c = cell % Columns + d[1];
			if (!Valid(r,c) || c < 2) continue;
			const int next = Index(r,c);
			if (rock[next] || !connected[next] || result[next] != Unreachable) continue;
			result[next] = result[cell] + 1;
			queue[tail++] = next;
		}
	}
	return result;
}

bool MineGrid::FindExcavation(int start, const std::array<bool, Count>& excluded, int& wall, int& stand) const
{
	wall = stand = -1;
	if (start < 0 || start >= Count || distance[start] >= Unreachable) return false;
	const auto walk = WorkDistances(start);
	std::tuple<int,int,int,int,int> best{};
	for (int target = 0; target < Count; ++target) {
		if (excluded[target] || !CanExcavate(target / Columns, target % Columns)) continue;
		MineGrid opened = *this;
		opened.rock[target] = false;
		opened.Rebuild();
		// 工兵为后续队伍开路：统计所有有效入口，不再要求自己绕到工地后仍能获利。
		int routeGain = 0;
		for (int row = 0; row < Rows; ++row) {
			const int entry = Index(row, Columns - 1);
			if (entrance[row] && distance[entry] < Unreachable)
				routeGain += distance[entry] - opened.distance[entry];
		}
		for (int direction = 0; direction < 4; ++direction) {
			const int r = target / Columns - kDirections[direction][0];
			const int c = target % Columns - kDirections[direction][1];
			if (!Valid(r,c)) continue;
			const int work = Index(r,c);
			if (walk[work] >= Unreachable || opened.distance[work] >= Unreachable) continue;
			const int ownGain = distance[start] - walk[work] - opened.distance[work];
			const int score = routeGain * kRouteBenefitWeight - walk[work];
			// 入口没有收益时仍可帮助已经深入矿道的自己；无收益的装饰墙永不施工。
			if (score <= 0 && ownGain <= 0) continue;
			const auto rank = std::make_tuple(-std::max(score, ownGain),walk[work],direction,target,work);
			if (wall >= 0 && rank >= best) continue;
			best = rank; wall = target; stand = work;
		}
	}
	return wall >= 0;
}

int MineGrid::NextWork(int start, int stand) const
{
	if (start < 0 || start >= Count) return -1;
	const auto distances = WorkDistances(stand);
	if (distances[start] == 0 || distances[start] >= Unreachable) return -1;
	for (const auto& d : kDirections) {
		const int r = start / Columns + d[0], c = start % Columns + d[1];
		if (!Valid(r,c)) continue;
		const int next = Index(r,c);
		if (distances[next] + 1 == distances[start]) return next;
	}
	return -1;
}
