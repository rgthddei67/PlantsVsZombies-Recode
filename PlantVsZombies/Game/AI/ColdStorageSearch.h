#pragma once
#include "Game/Zombie/ZombieMovementRules.h"

#include "ColdStorageStrategy.h"
#include "ColdStorageDiagnostics.h"
#include "Game/PlantDamageOrigin.h"
#include "Game/Board/IceProduction.h"
#include "Game/Plant/AttackGrowth.h"
#include "Game/Plant/IceStorageNutRules.h"
#include <array>
#include <atomic>
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

/** 数值化的一次性付费爆发；零范围禁用，阶段不含具体僵尸类型或实体引用。 */
struct PaidBurst {
	enum class Stage { READY, WINDUP, ACTIVE, RECOVERY, SPENT };
	Stage stage = Stage::READY;
	float range = 0, cost = 0, stopHealth = 0;
	float windup = 0, duration = 0, recovery = 0, retry = 0;
	float remaining = 0, retryRemaining = 0;
	float moveMultiplier = 1, biteMultiplier = 1, recoveryMoveMultiplier = 1;
};
/** 可付费修复的一类防具；health 属于总生命，不能沿用二类盾的绕盾/穿透语义。 */
struct ArmorRepair {
	float health = 0, maximum = 0, totalMaximum = 0, stopBodyHealth = 0;
	float remaining = 0, interval = 0, amount = 0, cost = 0;
};
/** 鼓手动作与目标的已提交层分别计时，来源死亡不撤销目标效果。 */
struct Drum {
	bool enabled = false, winding = false;
	float remaining = 0, stopHealth = 0;
};
/** 同行落种反应：装填与前摇独立计时，出膛后的脉冲不再依赖来源存活。 */
struct DeploymentSniper {
	bool enabled = false, aiming = false;
	float remaining = 0, stopHealth = 0;
	int targetID = 0;
	float targetX = 0, damage = 0;
	float muzzleOffset = 0; // 碰撞参考 X 到实际视觉枪口的偏移，像素；Board 转换后提供
};
/** 仪器破坏取消未提交仪式并切换过载；已提交的裂隙由独立队列持有。 */
struct Ritual {
	bool present = false, enabled = false, winding = false;
	float armor = 0, stopHealth = 0, remaining = 0;
	int releases = 0;
};
struct Unit {
	ColdStorageStrategy::SplashUnit body;
	float minimumMoveSpeed = 0, maximumMoveSpeed = 0; // 未出生的品种移速范围，px/游戏秒；已有实体为零，沿用实测速度
	bool birthMovementKnown = false; // 零移速也可能是合法出生阶段，不能把静止品种当成普通行走
	ZombieMovementRules::PositionCurve movementCurve;
	float movementCurveBase=0, movementCurveReference=0; // 世界基准与采样位置；保持已采样速度倍率，按推进位置更新车速
	PaidBurst burst;
	ArmorRepair repair;
	Drum drum;
	DeploymentSniper sniper;
	Ritual ritual;
	float adaptiveHelmet = 0;
	PlantDamageOrigin adaptedOrigin;
	std::vector<std::pair<int,float>> inspiration; // 来源身份与剩余游戏秒，同源刷新
	float drumSpeedAmplifier = 1; // 当前黄色冰道的非中性倍率放大，下次采样更新
	bool vehicleCrush = false; // 碰到抗碾压植物时使用承伤/推退契约，其他车战斗仍沿用原近似
	float productionRemaining = IceProduction::Interval, nextYield = IceProduction::InitialYield, biteDps = 50;
	float playerRefund = 0; // 只有正式付费单位死亡才返给植物方，免费召唤不计
	float shieldHealth = 0; // body.health 中的二类防具份额；本体/头盔归零时，剩余护盾不能维持存活
	float shieldedHitCap = 0, shieldedAshCap = 0; // 持盾时的单次伤害上限；零表示不封顶
	float fumeMultiplier = 1; // 目标自有的大喷家族倍率，破盾后仍适用
	bool blocksShieldBypass = false, blocksFumePiercing = false; // 只在护盾尚存时生效
	int id = 0; // 已有实体稳定 ID；新增候选以出生序列打破威胁并列
	float productionStopHealth = IceProduction::WorkerHealth / 3; // 对齐 Zombie::TakeBodyDamage 掉头阈值；掉头后不再生产
	float throwHealth = 0, throwAnchorX = 0, throwWindup = 1; // 尚持小鬼的投手：触发生命、半场锚点与预计前摇；零生命阈值禁用
	bool mowerImmune = false, consumesOtherMowers = false; // 单位自身的清洁车交互能力，不从购买价格猜测
};
struct Plant {
	PlantDamageOrigin damageOrigin;
	float maximumHealth = 0; // 裂隙按最高层原上限选择落点，不随当前残血重排
	int boundaryShards = 0;
	int hostileMirrors = 0; // 冰镜草已成型镜面，逐发拦截敌方直射弹
	float boundaryRecharge = 0, boundaryCharge = 0, boundaryBlockedUntil = 0;
	AttackGrowth growth; // perShot > 0 时按实际射击成长；DPS 不再冻结在采样时刻
	float growthSpeed = 1; // 不含菠萝领域的基础行动倍率，领域在时间线中独立推进
	int row = 0, column = 0, layer = 1;
	float x = 0, health = 0, dps = 0, reward = 0;
	float slowRate = 0, slowDuration = 0, stopDuty = 0;
	float sunPerSecond = 0;
	int rowRadius = 0;
	float range = 10000;
	bool multiTarget = false, around = false;
	bool melon = false, edible = true;
	float hitDamage = 20; // 等效单发伤害，用于将每击上限换算为 DPS；常规小弹丸默认 20
	bool bypassShield = false, fume = false; // 抛物绕盾/大喷穿盾由 Board 解析；西瓜使用 melon 的双层受伤语义
	int id = 0; // 主动能力的来源，推演中被消灭后不能继续释放
	float initialHealth = 0, productionAt = 0; // 削血分母与新生产株首次产出的时刻，游戏秒
	float repairMaximum = 0, repairAmount = 0, repairCost = 0, repairRecharge = 0, repairRemaining = 0;
	float repairBlockedUntil = 0, damageCredit = 0; // 暂停新修复的期限，以及可被回血撤回的削血得分
	bool repairAutomatic = false;
	float crushDamage = 0, immuneRemaining = 0, vehicleRetreat = 0;
	bool hasBurstProtection = false;
	IceStorageNutRules::Protection burstProtection; // 自有护体快照；无敌结束才启动冷却，就绪才统计伤害
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
	int remainingUses = -1; // 同 source 共用累计剩余次数，-1 表示不限；死亡不返还
	int quotaGroup = -1; // 非负时跨卡槽共享名额，如本卡与模仿者；缺省按 source
};
/** 一张可循环铲种的返阳光卡；候选格共享卡槽冷却，收益来自真实负阳光价格。 */
struct SunExchange {
	int sunGain = 0, iceCost = 0;
	float ready = 0, recharge = 1;
	std::vector<std::array<int,2>> cells;
};
/** 已在场的临时攻击领域；只有攻击/控制频率受益，生产、技能冷却不加速。 */
struct AttackAura {
	int plantID = 0, row = 0, column = 0;
	float active = 0, cooldown = 0, duration = 0, recharge = 0, bonus = 0;
	int iceCost = 0;
	float blockedUntil = 0; // 当前停机结束前不能新激活，既有领域仍计时
	bool automatic = false;
};
struct ShopOrder { int sunCost = 0, iceGain = 0; float delivery = 0; };
struct ConstructionStats {
	int movementBoundsApplied = 0; // 为经济生存推演采用出生移速边界的单位数，不额外增加候选或推演次数
	int planted = 0, exchanges = 0, orders = 0;
	float abilityIceSpent = 0; // 僵尸未来实际可付的技能费，计入支出，不提高成交返冰价
	int burstActivations = 0, auraActivations = 0, armorRepairs = 0, plantRepairs = 0;
	float armorRepairIce = 0, plantRepairIce = 0;
	int drumBeats = 0, drumRecipients = 0, precisionHits = 0;
	int deploymentShots = 0, deploymentHits = 0;
	int ritualReleases = 0, riftSummons = 0, riftRedirects = 0;
	float sunSpent = 0, iceSpent = 0, opponentAssets = 0;
	float exchangeSun = 0, exchangeIce = 0, orderSun = 0, orderIce = 0, pendingIce = 0;
};
struct Mower { int row = 0; float x = 0, width = 60, speed = 230; bool moving = false, active = true; };
struct Option { int type = 0, row = 0, cost = 0; Unit unit; ContextWeights preference{}; float firePreferenceScale = 1; };
struct Action { int option = 0; float delay = 0; };
/** 已付款但未出生的 current 下标与合法行；只允许改路或提前，不换兵、不退冰。 */
struct CommittedUnit { int unit = 0; std::array<bool, 6> legalRows{}; };
struct QueueRevision {
	int evaluated = 0, changed = 0; float beforeScore = 0, afterScore = 0;
	bool beforeBreach = false, afterBreach = false; // 已购队列也采用突破优先，评分仅用于相同结果
};
/** 同一 source 的格位是同一次反制的备选落点，共用冷却和资源；已提交动作不可改点。 */
struct Counter {
	ColdStorageStrategy::BlastThreat blast;
	int source = 0, sunCost = 0, iceCost = 0;
	float recharge = 10000, windup = 1;
	bool targeted = false; // 倭瓜先在种植格附近索敌，再在目标附近结算窄范围伤害
	int cellRow = -1, cellColumn = -1; // 新种灰烬的原落点；-1 表示无需空格的已有能力
	int plantID = 0; // 预存一次性反制的来源；释放前可被吃掉，爆炸后只消费这株一次
	int sharedSource = -1; // 额外共享的触发卡冷却，如多株预存毁灭共用咖啡；-1 表示无需第二张牌
	float sharedReady = 0, sharedRecharge = 0;
	float vulnerableSeconds = 0; // 从提交到清醒无敌的等待，游戏秒；已经清醒时为零
	bool stored = false; // 预存反制额外比较长期蓄爆，不假设小股诱饵一定能骗掉它
};
/** 已提交的裂隙，即使来源死亡也必须进入预测。 */
struct Rift { Unit unit; int column = 0; float remaining = 0; };
struct Snapshot {
	const std::atomic<bool>* cancellation = nullptr; // 仅后台任务自有的取消令牌；同步训练缺省为空，不改变评估结果
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
	float supplyRemaining = 0, supplyInterval = 0, supplyIce = 0; // 技能钱包的真实补给时序；不作为经营得分
	float houseX = 160, rightEdge = 1100;
	float gridLeft = 160, cellWidth = 80;
	int rows = 5, columns = 9;
	float discountRemaining = 0; // 已激活优惠的真实余时，届满后恢复原价
	bool precisionReady = false;
	int precisionTargetID = 0; // 本候选立即购买的技能；零表示保留资金
	int pendingPrecisionID = 0; // 已支付技能只结算原目标，不再次收费
	float pendingPrecisionRemaining = 0;
	float impWalkSpeed=20; // Board 从小鬼实际出生画像采样，纯数值夹具保留缺省值
	std::array<Unit,5> ritualSummons{};
	std::vector<Rift> rifts;
	std::vector<std::array<int,2>> riftCells; // 主线程给出的合法落点，不在后台查询 Board
	bool ritualWhiteout = false;
	std::vector<Unit> current;
	std::vector<CommittedUnit> committed;
	std::vector<Plant> plants;
	std::vector<Option> options;
	std::vector<Counter> counters;
	std::vector<RowStrike> rowStrikes;
	std::vector<AttackAura> attackAuras;
	std::vector<Construction> construction;
	std::vector<SunExchange> exchanges;
	std::vector<ShopOrder> shop;
	std::vector<Mower> mowers;
	std::array<ContextWeights, 6> context{};
};
struct Result {
	std::vector<CandidateStats> candidates; // 最终编队搜索阶段的候选统计；不含精准清除探测
	int precisionTargetID = 0, precisionEvaluated = 0;
	float precisionGain = 0; // 相对保留技能资金的增量评分；突破优先时可为负，以 features[2] 判断胜利
	bool expandedForecast = false; // 小队无增量收益后是否采用完整 v2 预测；避免混比两个时域的分数
	std::vector<Action> actions;
	Weights features{}, baselineFeatures{};
	Weights effectiveWeights{};
	StateFeatures stateInputs{};
	float score = 0, blastLoss = 0, preferenceScore = 0;
	float opponentAssets = 0, baselineOpponentAssets = 0, opponentScore = 0; // 与不增援基线比较，避免奖励本来就会发生的消耗
	int evaluated = 0;
	int capitalRejected = 0; // 资金充足时因大额亏损/清场风险被排除的候选数
	int routeEvaluated = 0, combinationEvaluated = 0; // 通用路线覆盖与组合探索次数，升级时合计两阶段
	float combinationBaseScore = 0, combinationBestScore = 0; // 最终阶段的组合探索前后评分
	bool combinationBaseBreach = false, combinationBestBreach = false; // 突破优先，因此胜出案评分可能下降
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
/** 护盾能减少的本体火力比例；Board 用它修正持盾单位的火力偏好上下文，保留无盾单位原语义。 */
float ShieldProtectionFraction(const Unit& unit, const Plant& plant);
/** 低于重组储备且增援没有足够增量收益时暂缓付款；已有部队的收益不能为新支出背书。 */
bool ShouldRegroup(const Result& result, int budget, int reserve);
/** 大额采购须保留可续战资本；现金与幸存兵力计入本金回收，清场风险仍要求回本或突破。 */
bool ShouldConserveCapital(const Result& result, int budget, int reserve);
/** 有限步位置推演；两类 hold 秒数只延迟未提交且非救险的反制，storedHoldSeconds 仅适用于预存来源。 */
Weights Evaluate(const Snapshot& state, const std::vector<Action>& plan, ConstructionStats* construction = nullptr, float counterHoldSeconds = 0, float storedHoldSeconds = 0);
/** 按合法兵种自由变异、配对及扩展后逐行比较；突破优先，同结果比较净收益，不迁移已有实体。 */
Result Search(const Snapshot& state, const Weights& weights, std::uint32_t seed);
/** 以原队列为保底比较合法重排；仅修改标记的未来单位，出生时间不晚于传入期限。 */
QueueRevision ReplanCommitted(Snapshot& state, const Weights& weights, std::uint32_t seed);
}
