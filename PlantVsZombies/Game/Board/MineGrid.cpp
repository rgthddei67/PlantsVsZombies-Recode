#include "Game/Board/MineGrid.h"
#include <algorithm>

void MineGrid::Initialize(int group, int revision)
{
	layoutGroup = std::clamp(group,0,4);
	layoutRevision = std::clamp(revision,0,1);
	constexpr const char* layout[Rows] = {
		"....#####", "..#......", "..#######", "..#......", "....#####"
	};
	constexpr const char* secondLayout[Rows] = {
		".........", "..#######", "..#######", "....#####", "..#......"
	};
	constexpr const char* thirdLayout[Rows] = {"..#######", "....##...", "..#....##", "....##...", "..#######"};
	constexpr const char* fourthLayout[Rows] = {"..####...", "..####.##", ".......##", "..####.##", "..####..."};
	// 三入口在第七列汇流，再由第四列分到第二/四行；左侧两列保持固定防守区。
	constexpr const char* finalLayout[Rows] = {"..####...", "....##.##", "..#......", "....##.##", "..####..."};
	for (int r = 0; r < Rows; ++r)
		for (int c = 0; c < Columns; ++c)
			rock[Index(r, c)] = (layoutGroup == 4 ? finalLayout : layoutGroup == 3 ? fourthLayout : layoutGroup == 2 ? thirdLayout : layoutGroup == 1 ? secondLayout : layout)[r][c] == '#';
	entrance = layoutGroup == 4 ? std::array<bool, Rows>{ true, false, true, false, true }
		: (layoutGroup == 1 || layoutGroup == 3) ? std::array<bool, Rows>{ true, false, false, false, true }
		: std::array<bool, Rows>{ false, true, false, true, false };
	if (layoutRevision == 1) {
		// 三条独立进攻方向逐步发展成四入口；直路、折返矿道和侧向连接同时存在。
		// 墙保护后方阵地也截断直射，开挖能扩展火力但同时给敌人缩短路线。
		constexpr const char* strategic[5][Rows] = {
			{"....#....", "..#...###", ".........", "..#....##", "....##..."},
			{".........", "..###..##", "....#....", "..#....##", "....##..."},
			{"..#......", "....#.###", "....#....", "..#....##", "........."},
			{".........", "..###....", ".......##", "..###....", "........."},
			{".....#...", "..#......", ".......##", "..##.....", "........."}
		};
		for (int r = 0; r < Rows; ++r) for (int c = 0; c < Columns; ++c)
			rock[Index(r,c)] = strategic[layoutGroup][r][c] == '#';
		entrance = layoutGroup < 3 ? std::array<bool,Rows>{true,false,true,false,true}
			: std::array<bool,Rows>{true,true,false,true,true};
	}
	Rebuild();
}

void MineGrid::Rebuild()
{
	distance.fill(Unreachable);
	connected.fill(false);
	std::array<int, Count> queue{};
	int head = 0, tail = 0;
	for (int r = 0; r < Rows; ++r) {
		if (IsRock(r, 0)) continue;
		const int cell = Index(r, 0);
		distance[cell] = 0;
		queue[tail++] = cell;
	}
	// 反向遍历：某格的前驱在右侧；竖向边仅允许位于固定防守区之外。
	while (head < tail) {
		const int cell = queue[head++], r = cell / Columns, c = cell % Columns;
		for (const auto delta : { std::array<int, 2>{0, 1}, { -1, 0 }, { 1, 0 } }) {
			const int nr = r + delta[0], nc = c + delta[1];
			if (!Valid(nr, nc) || IsRock(nr, nc) || (delta[0] && c < 2)) continue;
			const int next = Index(nr, nc);
			if (distance[next] != Unreachable) continue;
			distance[next] = distance[cell] + 1;
			queue[tail++] = next;
		}
	}
	// 返回入口采用四邻接图，防止左侧防守区或新挖死胡同困住魅惑单位。
	exitDistance.fill(Unreachable);
	head = tail = 0;
	for (int r = 0; r < Rows; ++r) {
		if (!entrance[r] || IsRock(r, Columns - 1)) continue;
		const int cell = Index(r, Columns - 1);
		exitDistance[cell] = 0;
		queue[tail++] = cell;
	}
	while (head < tail) {
		const int cell = queue[head++], r = cell / Columns, c = cell % Columns;
		for (const auto delta : { std::array<int, 2>{0, 1}, {0, -1}, {-1, 0}, {1, 0} }) {
			const int nr = r + delta[0], nc = c + delta[1];
			if (!Valid(nr, nc) || IsRock(nr, nc)) continue;
			const int next = Index(nr, nc);
			if (exitDistance[next] != Unreachable) continue;
			exitDistance[next] = exitDistance[cell] + 1;
			queue[tail++] = next;
		}
	}
	// 种植/施工的可达区域按无向四邻接计算，不把敌人的单向限制套到玩家空间。
	head = tail = 0;
	for (int r = 0; r < Rows; ++r) {
		if (IsRock(r, 0)) continue;
		const int cell = Index(r, 0);
		connected[cell] = true;
		queue[tail++] = cell;
	}
	while (head < tail) {
		const int cell = queue[head++], r = cell / Columns, c = cell % Columns;
		for (const auto delta : { std::array<int, 2>{0, 1}, {0, -1}, {-1, 0}, {1, 0} }) {
			const int nr = r + delta[0], nc = c + delta[1];
			if (!Valid(nr, nc) || IsRock(nr, nc)) continue;
			const int next = Index(nr, nc);
			if (connected[next]) continue;
			connected[next] = true;
			queue[tail++] = next;
		}
	}
}

bool MineGrid::CanExcavate(int row, int col) const
{
	if (col < 2 || !IsRock(row, col)) return false;
	for (const auto delta : { std::array<int, 2>{0, 1}, {0, -1}, {-1, 0}, {1, 0} }) {
		const int r = row + delta[0], c = col + delta[1];
		if (Valid(r, c) && connected[Index(r, c)]) return true;
	}
	return false;
}

int MineGrid::Next(int cell, bool preferDown) const
{
	if (cell < 0 || cell >= Count || rock[cell] || distance[cell] >= Unreachable) return -1;
	const int r = cell / Columns, c = cell % Columns;
	const int first = preferDown ? 1 : -1;
	for (const auto delta : { std::array<int, 2>{0, -1}, {first, 0}, {-first, 0} }) {
		const int nr = r + delta[0], nc = c + delta[1];
		if (!Valid(nr, nc) || IsRock(nr, nc) || (delta[0] && c < 2)) continue;
		const int next = Index(nr, nc);
		if (distance[next] + 1 == distance[cell]) return next;
	}
	return -1;
}

int MineGrid::NextExit(int cell, bool preferDown) const
{
	if (cell < 0 || cell >= Count || rock[cell] || exitDistance[cell] >= Unreachable) return -1;
	const int r = cell / Columns, c = cell % Columns, first = preferDown ? 1 : -1;
	for (const auto delta : { std::array<int, 2>{0, 1}, {first, 0}, {-first, 0}, {0, -1} }) {
		const int nr = r + delta[0], nc = c + delta[1];
		if (!Valid(nr, nc) || IsRock(nr, nc)) continue;
		const int next = Index(nr, nc);
		if (exitDistance[next] + 1 == exitDistance[cell]) return next;
	}
	return -1;
}

bool MineGrid::Validate() const
{
	for (int r = 0; r < Rows; ++r) {
		if (IsRock(r, 0) || IsRock(r, 1)) return false;
		if (entrance[r] && distance[Index(r, Columns - 1)] >= Unreachable) return false;
	}
	for (int cell = 0; cell < Count; ++cell) {
		if (rock[cell] || distance[cell] == Unreachable || cell % Columns == 0) continue;
		for (bool down : {false, true}) {
			int cursor = cell;
			for (int remaining = distance[cell]; remaining > 0; --remaining) {
				const int next = Next(cursor, down);
				if (next < 0 || distance[next] != remaining - 1) return false;
				cursor = next;
			}
		}
	}
	return true;
}
