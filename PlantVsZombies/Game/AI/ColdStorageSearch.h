#pragma once

#include "ColdStorageStrategy.h"
#include "Game/Board/IceProduction.h"
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

/** 可训练的编队搜索。仅消费数值快照；预测既不扣款也不使用正式游戏随机数。 */
namespace ColdStorageSearch {
inline constexpr int FeatureCount = 8;
using Weights = std::array<float, FeatureCount>;
inline constexpr int ContextCount = 8; // 偏置、友军伤势、友军密度、前墙、减速、火力、工人数、后排保护
using ContextWeights = std::array<float, ContextCount>;
inline constexpr int StateFeatureCount = 6; // 库存、预计生产、植物火力、植物经济、可用反制、无击杀时长
using StateFeatures = std::array<float, StateFeatureCount>;
/** 随局势调整收益评分的可训练线性层；零系数不指定任何攒钱门槛或兵种编队。 */
struct StateModel {
	std::array<Weights, StateFeatureCount> coefficients{};
	/** 每项系数须有限且在与基础评分相同的数值域内。 */
	bool IsValid() const;
};
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
	float throwHealth = 0, throwAnchorX = 0, throwWindup = 1; // 尚持小鬼的投手：触发生命、半场锚点与预计前摇；零生命阈值禁用
	bool mowerImmune = false, consumesOtherMowers = false; // 单位自身的清洁车交互能力，不从购买价格猜测
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
	float initialHealth = 0, productionAt = 0; // 削血分母与新生产株首次产出的时刻，游戏秒
	float assetValue = 0; // 当前生命对应的卡价折冰估值；终点再按后续剩余生命折价，不直接返给僵尸
};
/** 一次释放同时打击各行最高威胁目标；各行共用来源的一个充能周期。 */
struct RowStrike {
	int plantID = 0;
	float ready = 0, recharge = 20, damage = 0, splashDamage = 0, radius = 0;
};
/** 当前卡槽的合法建设落点；同 source 共用冷却，不能把每个格位当成独立牌。 */
struct Construction {
	Plant plant;
	RowStrike strike;
	int source = 0, sunCost = 0, iceCost = 0;
	float ready = 0, recharge = 1, firstSunDelay = 0;
};
/** 一张可循环铲种的返阳光卡；候选格共享卡槽冷却，收益来自真实负阳光价格。 */
struct SunExchange {
	int sunGain = 0, iceCost = 0;
	float ready = 0, recharge = 1;
	std::vector<std::array<int,2>> cells;
};
struct ShopOrder { int sunCost = 0, iceGain = 0; float delivery = 0; };
struct ConstructionStats {
	int planted = 0, exchanges = 0, orders = 0;
	float sunSpent = 0, iceSpent = 0, opponentAssets = 0;
	float exchangeSun = 0, exchangeIce = 0, orderSun = 0, orderIce = 0, pendingIce = 0;
};
struct Mower { int row = 0; float x = 0, width = 60, speed = 230; bool moving = false, active = true; };
struct Option { int type = 0, row = 0, cost = 0; Unit unit; ContextWeights preference{}; };
struct Action { int option = 0; float delay = 0; };
/** 已付款但未出生的 current 下标与合法行；只允许改路或提前，不换兵、不退冰。 */
struct CommittedUnit { int unit = 0; std::array<bool, 6> legalRows{}; };
struct QueueRevision { int evaluated = 0, changed = 0; float beforeScore = 0, afterScore = 0; };
/** 同一 source 的格位是同一次反制的备选落点，共用冷却和资源；已提交动作不可改点。 */
struct Counter {
	ColdStorageStrategy::BlastThreat blast;
	int source = 0, sunCost = 0, iceCost = 0;
	float recharge = 10000, windup = 1;
	bool targeted = false; // 倭瓜先在种植格附近索敌，再在目标附近结算窄范围伤害
	int cellRow = -1, cellColumn = -1; // 新种灰烬的原落点；-1 表示无需空格的已有能力
};
struct Snapshot {
	int searchVersion = 1; // 1 小队无预测增量收益时升级到 2；2 直接使用整队搜索与长时域预测
	bool netEconomy = false; // 新策略按统一冰价评价收入、残存投资和支出；旧配置保持原评分
	bool anticipateEconomy = false; // 显式考虑玩家循环经济卡与后续付费订冰，旧策略缺省关闭
	float opponentWeight = 0, sunIceValue = 0; // 对方终点资产差的可训练价值、商店阳光折冰率；权重零保持旧评分
	const ProductionCalibration* productionCalibration = nullptr;
	const StateModel* stateModel = nullptr;
	float noProgressSeconds = 0; // Board 已记录的连续无植物击杀时间，仅作模型输入
	int budget = 0, capacity = 0;
	int recoveryReserve = 0; // 正式 Board 指定的低库存重组门槛，零表示不启用
	bool allowWait = true; // 默认允许等待；Board 仅为可支付的后续兵种解锁路径请求出兵
	int playerSun = 0, playerIce = 0, incomingIce = 0;
	int playerSunLimit = (std::numeric_limits<int>::max)(), playerIceLimit = (std::numeric_limits<int>::max)(); // Board 提供正式容量；纯数值夹具可不设上限
	float incomingIceAt = 0;
	float houseX = 160, rightEdge = 1100;
	std::vector<Unit> current;
	std::vector<CommittedUnit> committed;
	std::vector<Plant> plants;
	std::vector<Option> options;
	std::vector<Counter> counters;
	std::vector<RowStrike> rowStrikes;
	std::vector<Construction> construction;
	std::vector<SunExchange> exchanges;
	std::vector<ShopOrder> shop;
	std::vector<Mower> mowers;
	std::array<ContextWeights, 6> context{};
};
struct Result {
	bool expandedForecast = false; // 小队无增量收益后是否采用完整 v2 预测；避免混比两个时域的分数
	std::vector<Action> actions;
	Weights features{}, baselineFeatures{};
	Weights effectiveWeights{};
	StateFeatures stateInputs{};
	float score = 0, blastLoss = 0, preferenceScore = 0;
	float opponentAssets = 0, baselineOpponentAssets = 0, opponentScore = 0; // 与不增援基线比较，避免奖励本来就会发生的消耗
	int evaluated = 0;
	int largestPlan = 0; // 实际评估过的最大付费编队，不是强制出兵数量
	bool regrouping = false; // 没有可接受的低库存增援；继续积累恢复资本
	ConstructionStats construction;
	float rawProduction = 0; // 未校准的产冰预期，供实际回报拟合
	float counterHoldSeconds = 0; // 本次保守评估采用的玩家灰烬等待习惯，游戏秒；不是僵尸出兵间隔
	ProductionFeatures productionInputs{};
	float formationBaseScore = 0; // 逐行对比前的最优自由编队评分
	std::array<float, 6> formationScores{}; // 同兵种/费用/时序整体投向各行的评分，按 tested 位掩码读取
	int formationTested = 0, formationRejected = 0, formationChosenRow = -1; // 拒绝位表示低库存收益不足；-1 保留自由编队
};

/** 验证参数尺寸以外的数值域，非法参数必须回退旧 AI。 */
bool ValidWeights(const Weights& weights);
/** 提取只读局势；baseline 的生产仍使用统一的未来 60 秒窗口。 */
StateFeatures DescribeState(const Snapshot& state, const Weights& baseline);
/** 在固定数值域内计算当前局势的评分权重；无模型时原样返回基础权重。 */
Weights ConditionWeights(const Weights& base, const StateFeatures& inputs, const StateModel* model);
/** 将经济项换成同一冰价的净收益；支出系数不可独立变异为奖励，残存投资至多按原价计。 */
Weights AccountForIce(const Weights& conditioned);
/** 低于重组储备且增援没有足够增量收益时暂缓付款；已有部队的收益不能为新支出背书。 */
bool ShouldRegroup(const Result& result, int budget, int reserve);
/** 有限步位置推演；counterHoldSeconds 只延迟未提交且非救险的玩家反制，不能延迟已种下的爆炸。 */
Weights Evaluate(const Snapshot& state, const std::vector<Action>& plan, ConstructionStats* construction = nullptr, float counterHoldSeconds = 0);
/** 自由变异后逐行比较同一编队的集中增援；保留观望、分路和时序，不迁移已有实体。 */
Result Search(const Snapshot& state, const Weights& weights, std::uint32_t seed);
/** 以原队列为保底比较合法重排；仅修改标记的未来单位，出生时间不晚于传入期限。 */
QueueRevision ReplanCommitted(Snapshot& state, const Weights& weights, std::uint32_t seed);
}
