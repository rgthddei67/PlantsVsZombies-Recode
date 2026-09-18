#include "Game/Board/MineGrid.h"
#include "Game/AI/MineWaveFormation.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <set>

/** 验证新布局多线连通、开挖单调性和同预算队伍的合法方向/前后排。 */
bool TestMineStrategy()
{
	for (int revision = 1; revision <= MineGrid::CurrentLayoutRevision; ++revision)
	for (int group = 0; group < 5; ++group) {
		MineGrid grid;
		grid.Initialize(group,revision);
		if (!grid.Validate()) { std::cerr << "invalid strategy layout " << group; return false; }
		std::set<int> exits;
		std::vector<std::set<int>> paths;
		int entrances = 0;
		for (int row = 0; row < MineGrid::Rows; ++row) if (grid.entrance[row]) {
			++entrances;
			for (bool down : {false,true}) {
				int cell = MineGrid::Index(row,8);
				std::set<int> path{cell};
				while (grid.distance[cell] > 0) { cell = grid.Next(cell,down); path.insert(cell); }
				paths.push_back(path);
				exits.insert(cell / MineGrid::Columns);
			}
		}
		std::cout << "strategy layout " << group << " entrances=" << entrances << " exits=" << exits.size() << '\n';
		if (entrances < 3 || exits.size() < 3) return false;
		if (revision >= 2 && group >= 2) {
			// 三个终点仍可能共用中途瓶颈；必须存在三条全程不相交的实际 Next 路径。
			auto disjoint = [](const auto& a, const auto& b) {
				return std::none_of(a.begin(),a.end(),[&](int cell) { return b.count(cell); });
			};
			bool threeIndependent = false;
			for (size_t a = 0; a < paths.size(); ++a)
				for (size_t b = a + 1; b < paths.size(); ++b)
					for (size_t c = b + 1; c < paths.size(); ++c)
						threeIndependent |= disjoint(paths[a],paths[b]) && disjoint(paths[a],paths[c]) && disjoint(paths[b],paths[c]);
			if (!threeIndependent) { std::cerr << "two-cell bottleneck " << group; return false; }
		}
		bool hasShortcut = false;
		for (int wall = 0; wall < MineGrid::Count; ++wall) if (grid.rock[wall]) {
			auto opened = grid; opened.rock[wall] = false; opened.Rebuild();
			if (!opened.Validate() || opened.entrance != grid.entrance) return false;
			for (int cell = 0; cell < MineGrid::Count; ++cell)
				if (opened.distance[cell] > grid.distance[cell]) return false;
			for (int row = 0; row < MineGrid::Rows; ++row) if (grid.entrance[row])
				hasShortcut |= opened.distance[MineGrid::Index(row,8)] < grid.distance[MineGrid::Index(row,8)];
		}
		if (revision >= 2 && group >= 2 && !hasShortcut) return false;
	}
	using namespace MineWaveFormation;
	for (bool concentrate : {false,true}) {
		std::vector<Unit> units{{9,0,21,FRONT},{8,0,21,SUPPORT},{7,0,21,EXCAVATOR},
			{1,0,21,REGULAR},{2,0,21,REGULAR},{3,0,21,REGULAR},{4,0,21,REGULAR},{5,0,1,FRONT}};
		const auto before = units;
		Arrange(units,{4,2,0,1,3},concentrate);
		std::set<int> rows, types;
		int supportRow = -1;
		for (const auto& unit : units) {
			if (!(unit.legalRows & (1 << unit.row))) return false;
			rows.insert(unit.row); types.insert(unit.type);
			if (unit.role == SUPPORT) supportRow = unit.row;
		}
		if (rows.size() < 3 || units.size() != before.size() || types.size() != before.size()) return false;
		for (const auto& unit : before) if (!types.count(unit.type)) return false;
		if (std::none_of(units.begin(),units.end(),[&](const Unit& unit) { return unit.role == FRONT && unit.row == supportRow; })) return false;
		if (RankOffset(FRONT) >= RankOffset(SUPPORT)) return false;
	}
	return true;
}
