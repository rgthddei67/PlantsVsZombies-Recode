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
inline constexpr int ProductionFeatureCount = 10; // 产冰预测、现有/新工人、生命、护卫、护卫投入、火力、减速、墙、库存
using ProductionFeatures = std::array<float, ProductionFeatureCount>;
/** 小型实战校准树，只折减简化推演的产冰预期；不生成资源或改变单位。 */
struct ProductionCalibration {
	struct Node { int feature = -1, left = -1, right = -1; float threshold = 0, value = 1; };
	std::vector<Node> nodes;
	/** 加载时检查有界、有限值和无环拓扑。 */
	bool IsValid() const;
	/** 对已通过 IsValid 的只读树推断倍率；不得传入尚未验证的资源。 */
	float Predict(const ProductionFeatures& features) const;
};
// 击杀返冰、削血、突破、存活投资、生产收入、支出、爆区损失、推进；只作训练起点。
inline constexpr Weights InitialWeights{3, 1, 120, 0.4f, 0.25f, -1, -2, 0.5f};

struct Unit {
	ColdStorageStrategy::SplashUnit body;
	float productionRemaining = IceProduction::Interval, nextYield = IceProduction::InitialYield, biteDps = 50;
	float playerRefund = 0; // 只有正式付费单位死亡才返给植物方，免费召唤不计
	int id = 0; // 已有实体稳定 ID；新增候选以出生序列打破威胁并列
	float productionStopHealth = IceProduction::WorkerHealth / 3; // 对齐 Zombie::TakeBodyDamage 掉头阈值；掉头后不再生产
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
	int id = 0; // 主动能力的来源，推演中被消灭后不能继续释放
};
/** 一次释放同时打击各行最高威胁目标；各行共用来源的一个充能周期。 */
struct RowStrike {
	int plantID = 0;
	float ready = 0, recharge = 20, damage = 0, splashDamage = 0, radius = 0;
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
	const ProductionCalibration* productionCalibration = nullptr;
	int budget = 0, capacity = 0;
	int recoveryReserve = 0; // 正式 Board 指定的低库存重组门槛，零表示不启用
	bool allowWait = true; // Board 的已存档观望时限到期且空场时，必须选择可支付行动
	int playerSun = 0, playerIce = 0, incomingIce = 0;
	float incomingIceAt = 0;
	float houseX = 160, rightEdge = 1100;
	std::vector<Unit> current;
	std::vector<Plant> plants;
	std::vector<Option> options;
	std::vector<Counter> counters;
	std::vector<RowStrike> rowStrikes;
	std::array<ContextWeights, 6> context{};
};
struct Result {
	std::vector<Action> actions;
	Weights features{}, baselineFeatures{};
	float score = 0, blastLoss = 0, preferenceScore = 0;
	int evaluated = 0;
	bool regrouping = false; // 没有可接受的低库存增援；继续积累恢复资本
	float rawProduction = 0; // 未校准的产冰预期，供实际回报拟合
	ProductionFeatures productionInputs{};
	float formationBaseScore = 0; // 逐行对比前的最优自由编队评分
	std::array<float, 6> formationScores{}; // 同兵种/费用/时序整体投向各行的评分，按 tested 位掩码读取
	int formationTested = 0, formationRejected = 0, formationChosenRow = -1; // 拒绝位表示低库存收益不足；-1 保留自由编队
};

/** 验证参数尺寸以外的数值域，非法参数必须回退旧 AI。 */
bool ValidWeights(const Weights& weights);
/** 低于重组储备且增援没有足够增量收益时暂缓付款；已有部队的收益不能为新支出背书。 */
bool ShouldRegroup(const Result& result, int budget, int reserve);
/** 有限步位置推演；直接承伤、邻行溅射、减速、破障和产冰联合评分。 */
Weights Evaluate(const Snapshot& state, const std::vector<Action>& plan);
/** 自由变异后逐行比较同一编队的集中增援；保留观望、分路和时序，不迁移已有实体。 */
Result Search(const Snapshot& state, const Weights& weights, std::uint32_t seed);
}
