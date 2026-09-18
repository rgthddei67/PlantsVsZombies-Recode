#include "MineExcavationTactics.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace MineExcavationTactics {
namespace {
	constexpr float kStep = 0.5f; // 推演步长，游戏秒；只用于策略估计，不代替正式战斗
	constexpr float kHorizon = 45; // 施工和后续进攻的总预测游戏秒
	constexpr float kMinimumGain = 8; // 最低净收益，避免微小近似误差触发施工
	constexpr float kHealthValue = 0.08f; // 剩余僵尸耐久折算进攻价值
	constexpr float kBreachValue = 1200; // 一只僵尸抵达房屋的进攻价值
	constexpr int kMaxCandidates = 12; // 最多比较的不同墙；先按纯地形收益稳定筛选
	constexpr std::array<std::array<int,2>,4> kDirections{{{0,-1},{-1,0},{1,0},{0,1}}};

	/** 冻结一株植物在候选地形上的覆盖；回声必须重建四向传播而非只看同排。 */
	std::uint64_t Coverage(const Plant& plant, const MineGrid& grid)
	{
		std::uint64_t mask = 0;
		const int row = plant.cell / MineGrid::Columns, col = plant.cell % MineGrid::Columns;
		if (plant.shape == AttackShape::ECHO) {
			std::array<int, MineGrid::Count> distance;
			distance.fill(MineGrid::Unreachable);
			std::array<int, MineGrid::Count> queue{};
			int head = 0, tail = 0;
			queue[tail++] = plant.cell; distance[plant.cell] = 0;
			while (head < tail) {
				const int cell = queue[head++];
				mask |= std::uint64_t{1} << cell;
				if (distance[cell] >= plant.range) continue;
				for (const auto& direction : kDirections) {
					const int r = cell / MineGrid::Columns + direction[0];
					const int c = cell % MineGrid::Columns + direction[1];
					if (!MineGrid::Valid(r,c)) continue;
					const int next = MineGrid::Index(r,c);
					if (grid.rock[next] || !grid.connected[next] || distance[next] != MineGrid::Unreachable) continue;
					distance[next] = distance[cell] + 1; queue[tail++] = next;
				}
			}
			return mask;
		}
		for (int r = std::max(0,row-plant.rowRadius); r <= std::min(MineGrid::Rows-1,row+plant.rowRadius); ++r) {
			const int left = plant.shape == AttackShape::LOCAL ? std::max(0,col-static_cast<int>(plant.range)) : col;
			for (int c = left; c < MineGrid::Columns && c <= col+plant.range; ++c) {
				if (plant.shape == AttackShape::LINE && grid.IsRock(r,c)) break;
				if (!grid.IsRock(r,c)) mask |= std::uint64_t{1} << MineGrid::Index(r,c);
			}
		}
		return mask;
	}

	struct Outcome { float score = 0; bool opened = false; };
	/** 以同一队伍推进基线/候选；先攻击后移动，施工死亡或接触阻挡均不能凭空开墙。 */
	Outcome Simulate(const Snapshot& snapshot, int wall, int stand)
	{
		auto plants = snapshot.plants;
		auto zombies = snapshot.zombies;
		MineGrid grid = snapshot.grid;
		std::vector<std::uint64_t> masks;
		for (const auto& plant : plants) masks.push_back(Coverage(plant,grid));
		std::array<bool,MaxZombies> breached{};
		std::array<float,MaxZombies> initialDistance{};
		for (size_t i = 0; i < zombies.size(); ++i) {
			const auto& z = zombies[i];
			const int cell = MineGrid::Index(std::clamp(static_cast<int>(std::lround(z.row)),0,4),
				std::clamp(static_cast<int>(std::lround(z.column)),0,8));
			initialDistance[i] = static_cast<float>(grid.distance[cell]);
		}
		Outcome result;
		float work = snapshot.workSeconds;
		bool task = wall >= 0;
		for (float time = 0; time < kHorizon; time += kStep) {
			std::array<int,MineGrid::Count> topPlants;
			topPlants.fill(-1);
			for (size_t p = 0; p < plants.size(); ++p) {
				const auto& plant = plants[p];
				if (plant.health <= 0 || plant.layer < 0) continue;
				int& top = topPlants[plant.cell];
				if (top < 0 || plant.layer > plants[top].layer) top = static_cast<int>(p);
			}
			std::array<float,MaxZombies> damage{}, slowUptime{};
			std::array<int,MaxZombies> cells{};
			for (size_t i = 0; i < zombies.size(); ++i) {
				const auto& z = zombies[i];
				cells[i] = z.column > 8.5f ? -1 : MineGrid::Index(
					std::clamp(static_cast<int>(std::lround(z.row)),0,4),
					std::clamp(static_cast<int>(std::lround(z.column)),0,8));
			}
			// 单体火力只分给每个覆盖行最靠前的一只；范围火力不因堆兵而被稀释。
			for (size_t p = 0; p < plants.size(); ++p) {
				const auto& plant = plants[p];
				if (plant.health <= 0 || plant.dps <= 0 || time < plant.shutdown) continue;
				std::array<int,MineGrid::Rows> front;
				front.fill(-1);
				for (size_t i = 0; i < zombies.size(); ++i) {
					if (zombies[i].health <= 0 || breached[i] || cells[i] < 0
						|| !(masks[p] & (std::uint64_t{1} << cells[i]))) continue;
					if (plant.multiTarget) {
						damage[i] += plant.dps * kStep;
						slowUptime[i] = std::max(slowUptime[i],plant.slowUptime);
					} else {
						int& target = front[cells[i] / MineGrid::Columns];
						if (target < 0 || zombies[i].column < zombies[target].column) target = static_cast<int>(i);
					}
				}
				if (!plant.multiTarget) for (int i : front) if (i >= 0) {
					damage[i] += plant.dps * kStep;
					slowUptime[i] = std::max(slowUptime[i],plant.slowUptime);
				}
			}
			for (size_t i = 0; i < zombies.size(); ++i) {
				auto& z = zombies[i];
				if (z.health <= 0 || breached[i]) continue;
				const bool fog = time < snapshot.fogRemaining && z.prism <= time
					&& z.column >= snapshot.fogFirstColumn - 0.5f;
				z.health -= damage[i] * (fog ? 1-snapshot.fogReduction : 1);
				if (z.health <= 0 || time < z.stun) continue;
				const float rate = time < z.slow ? 0.5f : 1-0.5f*slowUptime[i];
				int blocker = -1;
				// 按格索引接触层，避免每只僵尸每步重扫整张植物表。
				const int nearest = cells[i];
				for (int direction = -1; nearest >= 0 && direction < 4; ++direction) {
					const int r = nearest / MineGrid::Columns + (direction < 0 ? 0 : kDirections[direction][0]);
					const int c = nearest % MineGrid::Columns + (direction < 0 ? 0 : kDirections[direction][1]);
					if (!MineGrid::Valid(r,c)) continue;
					const int p = topPlants[MineGrid::Index(r,c)];
					if (p < 0) continue;
					const auto& plant = plants[p];
					const float dx = z.column - plant.cell % MineGrid::Columns;
					const float dy = z.row - plant.cell / MineGrid::Columns;
					if (dx*dx+dy*dy > 0.55f*0.55f) continue;
					if (blocker < 0 || plant.layer > plants[blocker].layer) blocker = static_cast<int>(p);
				}
				if (blocker >= 0) {
					if (i == 0 && task) task = false; // 正式工兵开始啃食会放弃本次施工
					auto& plant = plants[blocker];
					const float dps = z.smashSeconds > 0
						? snapshot.plants[blocker].health / z.smashSeconds : z.dps;
					plant.health -= dps * kStep * rate;
					if (plant.health <= 0) {
						int& top = topPlants[plant.cell]; top = -1;
						for (size_t p = 0; p < plants.size(); ++p)
							if (plants[p].cell == plant.cell && plants[p].health > 0 && plants[p].layer >= 0
								&& (top < 0 || plants[p].layer > plants[top].layer)) top = static_cast<int>(p);
					}
					continue;
				}
				if (z.column > 8) { z.column = std::max(8.0f,z.column-z.speed*kStep*rate); continue; }
				const int cell = cells[i];
				if (z.target < 0) {
					if (i == 0 && task && cell == stand) {
						work -= kStep * rate;
						if (work <= 0) {
							grid.rock[wall] = false; grid.Rebuild(); result.opened = true; task = false;
							for (size_t p = 0; p < plants.size(); ++p) masks[p] = Coverage(plants[p],grid);
						}
						continue;
					}
					if (z.column <= 0) {
						breached[i] = true;
						// 两路都能抵达房屋时仍奖励提前突破，不能在时域末端把捷径价值抹平。
						result.score += kBreachValue*(1+(kHorizon-time)/kHorizon);
						continue;
					}
					z.target = i == 0 && task ? grid.NextWork(cell,stand) : grid.Next(cell,(z.id&1)!=0);
				}
				if (z.target < 0) continue;
				const float dx = z.target % MineGrid::Columns-z.column, dy = z.target / MineGrid::Columns-z.row;
				const float length = std::sqrt(dx*dx+dy*dy);
				const float move = z.speed*kStep*rate*(i == 0 && task ? snapshot.approachMultiplier : 1);
				if (length <= move) {
					z.column = static_cast<float>(z.target % MineGrid::Columns);
					z.row = static_cast<float>(z.target / MineGrid::Columns); z.target = -1;
				} else { z.column += dx*move/length; z.row += dy*move/length; }
			}
		}
		for (size_t p = 0; p < plants.size(); ++p)
			result.score += snapshot.plants[p].value * (1-std::max(0.0f,plants[p].health)/snapshot.plants[p].health);
		for (size_t i = 0; i < zombies.size(); ++i) {
			const auto& z = zombies[i];
			if (z.health <= 0) continue;
			const int cell = MineGrid::Index(std::clamp(static_cast<int>(std::lround(z.row)),0,4),
				std::clamp(static_cast<int>(std::lround(z.column)),0,8));
			const float progress = std::clamp(initialDistance[i]-grid.distance[cell],0.0f,10.0f);
			result.score += kHealthValue*z.health*(1+progress/10);
		}
		return result;
	}
}

Result Choose(const Snapshot& snapshot)
{
	Result result;
	if (snapshot.zombies.empty() || snapshot.zombies.size() > MaxZombies || snapshot.plants.size() > MaxPlants) return result;
	// 空防线保留为后续波次缩短矿道的原有行为；有植物时必须通过战斗收益门槛。
	if (snapshot.plants.empty()) {
		snapshot.grid.FindExcavation(snapshot.start,snapshot.excluded,result.wall,result.stand);
		return result;
	}
	result.baseline = Simulate(snapshot,-1,-1).score;
	float best = kMinimumGain;
	auto excluded = snapshot.excluded;
	for (int i = 0; i < kMaxCandidates; ++i) {
		int wall = -1, stand = -1;
		if (!snapshot.grid.FindExcavation(snapshot.start,excluded,wall,stand)) break;
		excluded[wall] = true; ++result.candidates;
		const auto outcome = Simulate(snapshot,wall,stand);
		const float gain = outcome.score-result.baseline;
		if (!outcome.opened || gain <= best) continue;
		best = gain; result.wall = wall; result.stand = stand; result.gain = gain;
	}
	return result;
}
}
