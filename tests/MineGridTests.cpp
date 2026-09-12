#include "Game/Board/MineGrid.h"
#include <array>
#include <iostream>

/** 穷举9-1全部可拆岩壁子集，验证连通性、防环和不向右/不穿墙的约束。 */
int main()
{
	// 第二组的双入口必须保留不同长度；逐墙开凿不能增加任一已有通路距离。
	MineGrid second;
	second.Initialize(1);
	if (!second.Validate() || second.distance[8] != 8 || second.distance[44] != 9
		|| !second.entrance[0] || !second.entrance[4]) return 8;
	int secondWalls = 0;
	for (int wall = 0; wall < MineGrid::Count; ++wall) {
		if (!second.rock[wall]) continue;
		++secondWalls;
		MineGrid dug = second;
		dug.rock[wall] = false;
		dug.Rebuild();
		if (!dug.Validate()) return 9;
		for (int cell = 0; cell < MineGrid::Count; ++cell)
			if (dug.distance[cell] > second.distance[cell]) return 10;
	}
	if (secondWalls != 20) return 11;
	// 第三组双入口先汇合再分流；逐墙施工必须仍有合法回家和返回入口的路径。
	MineGrid third;
	third.Initialize(2);
	int thirdWalls = 0;
	if (!third.Validate() || !third.entrance[1] || !third.entrance[3]) return 12;
	for (int wall = -1; wall < MineGrid::Count; ++wall) {
		if (wall >= 0 && !third.rock[wall]) continue;
		MineGrid dug = third;
		if (wall >= 0) { ++thirdWalls; dug.rock[wall] = false; dug.Rebuild(); }
		if (!dug.Validate()) return 13;
		for (int cell = 0; cell < MineGrid::Count; ++cell) {
			if (dug.rock[cell] || !dug.connected[cell]) continue;
			if (third.distance[cell] < MineGrid::Unreachable && dug.distance[cell] > third.distance[cell]) return 14;
			for (bool down : {false,true}) {
				int cursor = cell;
				for (int remaining = dug.exitDistance[cell]; remaining > 0; --remaining) {
					cursor = dug.NextExit(cursor,down);
					if (cursor < 0 || dug.rock[cursor] || dug.exitDistance[cursor] != remaining-1) return 15;
				}
				if (!dug.entrance[cursor/MineGrid::Columns] || cursor%MineGrid::Columns != 8) return 16;
			}
		}
	}
	if (thirdWalls != 21) return 17;
	// 第四组上下入口汇流；任一单墙开凿后都不得增加原路线距离或破坏返回入口路径。
	MineGrid fourth;
	fourth.Initialize(3);
	if (!fourth.Validate() || !fourth.entrance[0] || !fourth.entrance[4]
		|| fourth.distance[8]!=10 || fourth.distance[44]!=10) return 18;
	int fourthWalls=0;
	for (int wall=0; wall<MineGrid::Count; ++wall) {
		if (!fourth.rock[wall]) continue;
		++fourthWalls;
		MineGrid dug=fourth;
		dug.rock[wall]=false; dug.Rebuild();
		if (!dug.Validate()) return 19;
		for (int cell=0; cell<MineGrid::Count; ++cell)
			if (dug.distance[cell]>fourth.distance[cell]
				|| (dug.connected[cell] && !dug.rock[cell] && dug.exitDistance[cell]>=MineGrid::Unreachable)) return 20;
	}
	if (fourthWalls!=22) return 21;
	// 收官三入口先汇流再分到两条防线；逐墙开凿不能破坏任一入口和魅惑退路。
	MineGrid finale;
	finale.Initialize(4);
	if (!finale.Validate() || finale.entrance != std::array<bool,5>{true,false,true,false,true}
		|| finale.distance[8] != 11 || finale.distance[26] != 9 || finale.distance[44] != 11) return 22;
	for (int wall = 0; wall < MineGrid::Count; ++wall) {
		if (!finale.rock[wall]) continue;
		MineGrid dug = finale;
		dug.rock[wall] = false;
		dug.Rebuild();
		if (!dug.Validate()) return 23;
		for (int cell = 0; cell < MineGrid::Count; ++cell)
			if (dug.distance[cell] > finale.distance[cell]
				|| (dug.connected[cell] && !dug.rock[cell] && dug.exitDistance[cell] >= MineGrid::Unreachable)) return 24;
	}
	MineGrid initial;
	initial.Initialize();
	if (!initial.Validate() || initial.distance[17] != 9 || initial.distance[35] != 9) return 1;
	std::array<int, MineGrid::Count> walls{};
	int count = 0;
	for (int cell = 0; cell < MineGrid::Count; ++cell) if (initial.rock[cell]) walls[count++] = cell;
	const unsigned combinations = 1u << count;
	for (unsigned mask = 0; mask < combinations; ++mask) {
		MineGrid grid = initial;
		for (int bit = 0; bit < count; ++bit) if (mask & (1u << bit)) grid.rock[walls[bit]] = false;
		grid.Rebuild();
		if (!grid.Validate()) { std::cerr << "Invalid excavation mask " << mask; return 2; }
		for (int cell = 0; cell < MineGrid::Count; ++cell) {
			if (initial.distance[cell] < MineGrid::Unreachable && grid.distance[cell] > initial.distance[cell]) return 3;
			for (bool down : {false, true}) {
				int cursor = cell;
				if (!grid.rock[cell] && grid.connected[cell]) {
					for (int remaining = grid.exitDistance[cell]; remaining > 0; --remaining) {
						cursor = grid.NextExit(cursor, down);
						if (cursor < 0 || grid.exitDistance[cursor] != remaining - 1) return 6;
					}
					if (!grid.entrance[cursor / MineGrid::Columns] || cursor % MineGrid::Columns != 8) return 7;
				}
				const int next = grid.Next(cell, down);
				if (next < 0) continue;
				if (grid.rock[next] || next % MineGrid::Columns > cell % MineGrid::Columns) return 4;
				if (cell % MineGrid::Columns < 2 && next != cell - 1) return 5;
			}
		}
	}
	std::cout << "Validated " << combinations << " excavation layouts and both tie preferences\n";
	return 0;
}
