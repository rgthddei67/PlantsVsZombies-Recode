#pragma once

#include "ColdStorageStrategy.h"
#include "Game/Board/IceProduction.h"
#include <array>
#include <cstdint>
#include <vector>

/** 可训练的编队搜索。仅消费数值快照；预测既不扣款也不使用正式游戏随机数。 */
namespace ColdStorageSearch {
inline constexpr int FeatureCount = 8;
using Weights = std::array<float, FeatureCount>;
inline constexpr int ContextCount = 8; // 偏置、友军伤势、友军密度、前墙、减速、火力、工人数、后排保护
using ContextWeights = std::array<float, ContextCount>;
// 击杀返冰、削血、突破、存活投资、生产收入、支出、爆区损失、推进；只作训练起点。
inline constexpr Weights InitialWeights{3, 1, 120, 0.4f, 0.25f, -1, -2, 0.5f};

struct Unit {
	ColdStorageStrategy::SplashUnit body;
	float productionRemaining = IceProduction::Interval, nextYield = IceProduction::InitialYield, biteDps = 50;
	float playerRefund = 0; // 只有正式付费单位死亡才返给植物方，免费召唤不计
};
struct Plant {
	int row = 0, column = 0, layer = 1;
	float x = 0, health = 0, dps = 0, reward = 0;
	float slowRate = 0, slowDuration = 0, stopDuty = 0;
	float sunPerSecond = 0;
	int rowRadius = 0;
	float range = 10000;
	bool multiTarget = false, around = false;
	bool melon = false, edible = true;
};
struct Option { int type = 0, row = 0, cost = 0; Unit unit; ContextWeights preference{}; };
struct Action { int option = 0; float delay = 0; };
/** 同一 source 的格位是同一次反制的备选落点，共用冷却和资源；已提交动作不可改点。 */
struct Counter {
	ColdStorageStrategy::BlastThreat blast;
	int source = 0, sunCost = 0, iceCost = 0;
	float recharge = 10000, windup = 1;
	bool targeted = false; // 倭瓜先在种植格附近索敌，再在目标附近结算窄范围伤害
};
struct Snapshot {
	int budget = 0, capacity = 0;
	bool allowWait = true; // Board 的已存档观望时限到期且空场时，必须选择可支付行动
	int playerSun = 0, playerIce = 0, incomingIce = 0;
	float incomingIceAt = 0;
	float houseX = 160, rightEdge = 1100;
	std::vector<Unit> current;
	std::vector<Plant> plants;
	std::vector<Option> options;
	std::vector<Counter> counters;
	std::array<ContextWeights, 6> context{};
};
struct Result {
	std::vector<Action> actions;
	Weights features{};
	float score = 0, blastLoss = 0;
	int evaluated = 0;
};

/** 验证参数尺寸以外的数值域，非法参数必须回退旧 AI。 */
bool ValidWeights(const Weights& weights);
/** 有限步位置推演；直接承伤、邻行溅射、减速、破障和产冰联合评分。 */
Weights Evaluate(const Snapshot& state, const std::vector<Action>& plan);
/** 从空计划和随机动作序列变异搜索，可自由改变兵种、行、出生延迟及队伍长度。 */
Result Search(const Snapshot& state, const Weights& weights, std::uint32_t seed);
}
