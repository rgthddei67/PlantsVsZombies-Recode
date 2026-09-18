#include "Game/AI/MineExcavationTactics.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>

namespace {
	/** 两条对称石墙后分别放经济区和回声阵，实际队伍从上下入口跟进。 */
	MineExcavationTactics::Snapshot Fixture(bool lowerEconomy)
	{
		using namespace MineExcavationTactics;
		Snapshot s;
		s.grid.Initialize(3);
		for (int row : {0,4}) for (int col = 2; col <= 4; ++col) s.grid.rock[MineGrid::Index(row,col)] = false;
		s.grid.Rebuild(); s.start = MineGrid::Index(2,6);
		for (int row : {0,4}) for (int col = 0; col < 5; ++col) {
			Plant p;
			p.cell = MineGrid::Index(row,col); p.health = 300;
			const bool economy = row == (lowerEconomy ? 4 : 0);
			p.value = economy ? 200.0f : 300.0f; p.dps = economy ? 0.0f : 50.0f;
			p.shape = AttackShape::ECHO; p.multiTarget = true; p.range = 6;
			s.plants.push_back(p);
		}
		Zombie worker;
		worker.id = 0; worker.row = 2; worker.column = 6; worker.health = 1800;
		worker.speed = 0.2f; worker.dps = 50;
		s.zombies.push_back(worker);
		for (int row : {0,4}) for (int i = 0; i < 4; ++i) {
			Zombie z = worker; z.id = static_cast<int>(s.zombies.size());
			z.row = static_cast<float>(row); z.column = 8+0.45f*i; z.health = 1370;
			s.zombies.push_back(z);
		}
		return s;
	}
}

/** 行镜像应翻转选墙；拒绝无兵力的火力陷阱、不可完成施工与被预留候选，并测预算上限。 */
bool TestMineExcavationTactics()
{
	using namespace MineExcavationTactics;
	for (bool lower : {false,true}) {
		auto s = Fixture(lower);
		const auto r = Choose(s);
		std::cout << "mirror=" << lower << " wall=" << r.wall << " gain=" << r.gain << " baseline=" << r.baseline << '\n';
		if (r.wall != (lower ? 41 : 5) || r.gain <= 0) return false;
		const auto repeated = Choose(s);
		if (repeated.wall != r.wall || repeated.gain != r.gain) return false;
		s.excluded[r.wall] = true;
		if (Choose(s).wall == r.wall) return false;
		s = Fixture(lower); s.zombies[0].health = 1;
		Plant ambush;
		ambush.cell = s.start; ambush.health = 300; ambush.dps = 1000; ambush.layer = -1;
		ambush.shape = AttackShape::ECHO; ambush.multiTarget = true; ambush.range = 6;
		s.plants.push_back(ambush);
		if (Choose(s).wall >= 0) return false;
	}
	auto weak = Fixture(false);
	// 只开放通向回声区的候选；少量残血兵力必须继续原路线，不能把缩短路程当作突破。
	weak.excluded.fill(true); weak.excluded[41] = false;
	for (auto& z : weak.zombies) z.health = 100;
	if (Choose(weak).wall >= 0) return false;
	auto fortified = Fixture(false);
	for (auto& p : fortified.plants) {
		p.dps = 50; p.value = 300;
		if (p.cell % MineGrid::Columns == 4) { p.dps = 0; p.health = 4000; p.value = 50; }
	}
	fortified.zombies.resize(1);
	if (Choose(fortified).wall >= 0) return false;
	for (int row : {0,4}) for (int i = 0; i < 3; ++i) {
		Zombie giant = fortified.zombies.front();
		giant.id = static_cast<int>(fortified.zombies.size()); giant.row = static_cast<float>(row);
		giant.column = 7; giant.health = 6000; giant.smashSeconds = 4;
		fortified.zombies.push_back(giant);
	}
	const auto supported = Choose(fortified);
	std::cout << "fortified supported wall=" << supported.wall << " gain=" << supported.gain << '\n';
	if (supported.wall < 0) return false;
	auto empty = Fixture(false); empty.plants.clear();
	if (Choose(empty).wall != 5) return false;
	auto safe = empty;
	safe.grid.Initialize(); safe.start = MineGrid::Index(1,3); safe.zombies.resize(1);
	safe.zombies[0].row = 1; safe.zombies[0].column = 3;
	Plant remoteBlocker;
	remoteBlocker.cell = MineGrid::Index(3,1); remoteBlocker.health = 4000; remoteBlocker.value = 50;
	safe.plants.push_back(remoteBlocker);
	if (Choose(safe).wall != 11) return false; // 无交战风险，两路都能到家仍应识别提前突破
	// 满额实体和多次同步决策测最大耗时，避免只看平均 FPS 隐藏动作帧尖峰。
	auto crowded = Fixture(false);
	while (crowded.zombies.size() < MaxZombies) {
		auto z = crowded.zombies.back(); z.id = static_cast<int>(crowded.zombies.size());
		z.column = 8; crowded.zombies.push_back(z);
	}
	while (crowded.plants.size() < MaxPlants) {
		auto p = crowded.plants[crowded.plants.size()%10]; crowded.plants.push_back(p);
	}
	// 搜索固定种子的密集候选地形；与正式游戏 RNG 完全隔离。
	std::mt19937 random(137);
	int maxCandidates = 0;
	for (int sample = 0; sample < 400; ++sample) {
		MineGrid grid;
		for (int cell = 0; cell < MineGrid::Count; ++cell)
			grid.rock[cell] = cell%9 >= 2 && cell%9 < 8 && random()%3 == 0;
		grid.rock[crowded.start] = false; grid.Rebuild();
		auto excluded = crowded.excluded;
		int count = 0, wall = -1, stand = -1;
		while (grid.FindExcavation(crowded.start,excluded,wall,stand)) { excluded[wall] = true; ++count; }
		if (count > maxCandidates) { maxCandidates = count; crowded.grid = grid; }
	}
	for (auto& z : crowded.zombies) z.health = 100000; // 保持所有对象活到时域末端，避免早死低估开销
	for (auto& p : crowded.plants) { p.dps = 0.1f; p.health = 100000; }
	long long maxMicros = 0;
	for (int i = 0; i < 8; ++i) {
		const auto begin = std::chrono::steady_clock::now();
		Choose(crowded);
		maxMicros = std::max(maxMicros,static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now()-begin).count()));
	}
	std::cout << "bounded tactics candidate pool=" << maxCandidates << " max decision us=" << maxMicros << '\n';
	return true;
}
