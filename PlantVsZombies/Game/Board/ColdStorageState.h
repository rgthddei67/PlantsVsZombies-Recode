#pragma once

#include "Game/Zombie/ZombieType.h"
#include "Game/AI/ColdStorageDiagnostics.h"
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

/** 冷藏站出兵的已付款事务；出生之前仍占敌方兵力和同时名额。 */
struct ColdStorageDeployment {
	ZombieType type = ZombieType::ZOMBIE_NORMAL;
	int row = 0;
	int cost = 0;
	float remaining = 0.0f;
	int wave = 0; // 原付款批次；滚动增援不能把旧队列的产冰归入新波
	std::uint64_t ticket = 0; // 本次 Board 内的队列身份；读档重新编号，后台结果不能改到后来入队的单位
};

/** 最近经营窗口中的实际收支；正数金额，补给与预测收益不在此登记。 */
struct ColdStorageCashFlow {
	float at = 0.0f;
	int production = 0;
	int spent = 0;
};

/** 训练用实际产冰事件；批次排除后来工人的收入，不参与钱包或存档。 */
struct ColdStorageProductionEvent { float at = 0; int wave = 0, amount = 0; };

/** 战前支援按钮的稳定身份；组合按 1 << (身份 - 1) 入档。 */
enum class ColdStorageOpeningBonus { NONE = 0, ELITE_QUOTA = 1, PREPARATION = 2, CARD_RECHARGE = 3 };

/** Board 独占的冰块经济与指挥官状态；展示层只读，不另存资源余额。 */
struct ColdStorageState {
	int openingBonusMask = 0; // 已选支援位图；选择中的第一项尚不生效
	bool openingBonusSelectionComplete = true; // 区分尚待三选二和主动无增益，也兼容旧档单选
	std::uint64_t nextTicket = 0;
	bool planning = false; // 后台计算尚未领取；不入档、不代表已付款
	int planningStarted = 0, planningApplied = 0, planningDiscarded = 0;
	std::array<int,static_cast<int>(ColdStorageSearch::PlanDiscardReason::Count)> planningDiscardReasons{}; // 多原因可累计，不入档
	int planningLastDiscardMask = 0;
	float planningLastAgeMs = 0;
	std::vector<ColdStorageSearch::CandidateStats> searchUnitCandidates; // 含兵种/行的整案收益，最终阶段、精准清除前
	std::vector<ColdStorageSearch::WorkerForecastTrace> searchWorkerTrace; // 显式诊断轨迹，不入档
	std::vector<ColdStorageSearch::CounterForecastTrace> searchCounterTrace; // 显式诊断轨迹，不入档
	double planningWorkerMs = 0, planningMainMaxMs = 0; // 后台总耗时/主线程决策入口最大耗时，毫秒
    double planningBudgetMs = 0; // 本轮实时墙钟预算，毫秒，不入档
	int planningWorkerThreads = 0, planningParallelPlans = 0; // 最近领取任务的计算线程数/辅助线程完整候选数，不入档
    bool planningTimeLimited = false; // 是否因预算停止扩展候选，不代表返回了不完整的预测
	static constexpr int RecoveryReserveIce = 48; // 能重新组织护卫与制冰工的最低储备，冰块
	int playerIce = 200; // 开局冷库可支撑完整五路基础阵型，后续依赖采购
	int enemyIce = 0;
	int initialEnemyIce = 0;
	int difficulty = 1;
	int orderIce = 0;
	float orderRemaining = 0.0f;
	float supplyRemaining = 30.0f;
	float decisionRemaining = 45.0f; // 首轮进攻前的布阵时间，游戏秒
	float elapsed = 0.0f;
	float discountRemaining = 0.0f; // 玩家全场减费剩余游戏秒；多次使用刷新，不叠加倍率
	float interferenceRemaining = 0.0f; // 玩家时间干扰剩余游戏秒，禁止提交新时间锚
	float interferenceCooldownRemaining = 0.0f; // 商店技能独立冷却；入档，暂停不推进
	float strikeCooldownRemaining = 0.0f; // 敌方指挥官全局冷却，游戏秒；不属于任何僵尸实体
	int strikeTargetID = -1; // 已付费瞄准的稳定植物 ID；-1 表示无在途打击
	std::vector<int> strikeAdditionalTargetIDs; // 同批另0-2株已付款目标；主ID保留旧档兼容，不因目标消失重选
	float strikeAimRemaining = 0.0f; // 不可打断的瞄准提示剩余游戏秒；随目标移动，不换靶
	float incomeIdleSeconds = 0.0f; // 连续没有实际制冰/击杀收入的游戏秒；定时补给和派兵不重置，入档
	float plantKillIdleSeconds = 0.0f; // 连续没有消灭植物的游戏秒；新局/无历史旧档从零计时，入档
	std::deque<ColdStorageCashFlow> incomeWindow; // 最近经营窗口的实际制冰及购买事务，入档；Update 移除过期记录
	float assaultCooldown = 0.0f; // 总攻后的重新组织时间，游戏秒；读档不重置
	float dispatchQuietSeconds = 0.0f; // 距上次正式派兵的游戏秒；旧 AI 使用，学习分支不据此强迫出兵
	int spent = 0;
	int supplied = 0;
	int searchEngineerBlocks = 0, searchThunderStuns = 0;
	float searchEngineerReloadIce = 0;
	int workerIncome = 0; // 制冰工累计为敌方生产的冰块
	std::deque<ColdStorageProductionEvent> productionEvents; // 最近 120 秒，上限 4096 条，仅 AutoTest 采集
	int playerProductionIncome = 0; // 薄荷与魅惑制冰工累计生产的冰块
	float predictedProduction = 0.0f; // 本次决策时域内可存活生产的预测收入
	float economyValue = 0.0f; // 最佳经济投资的预计净收益
	std::array<float, 6> economyNetByRow{}; // 各路最优经营组合净收益，诊断不入档
	std::array<float, 6> economyBlastLossByRow{}; // 各路最优组合的爆炸风险折损冰量
	std::array<int, 6> economyGuardCostByRow{}; // 最优组合新购护卫冰价，零表示无需新增护卫
	std::array<float, 6> economyEntryDelayByRow{}; // 最优组合的工人跟进延迟，游戏秒，诊断不入档
	std::array<float, 6> economyCoverByRow{}; // 最优组合预计受前排保护时间，游戏秒，诊断不入档
	int economyRow = -1; // 本次经济路线，诊断投影不入档
	int killIncome = 0;
	int playerKillIncome = 0; // 玩家通过消灭付费敌人累计回收的冰块
	int deployments = 0;
	std::map<ZombieType, int> deploymentTypes; // 本次运行各兵种正式出生数量，仅诊断，不改变存档事务
	int decisions = 0;
	int lastAttackRow = -1;
	int candidatesEvaluated = 0;
	int searchRouteEvaluated = 0, searchCombinationEvaluated = 0; // 通用路线/组合比较次数，仅诊断不入档
	int searchCohortEvaluated = 0; // 组合预算中的通用成批候选数，仅诊断不入档
	int searchUnevenMixEvaluated = 0, searchDuplicatesSkipped = 0; // 通用比例覆盖及重复案复用，仅诊断不入档
	int searchPaidCounterCasts = 0; // 预测中实际提交的付费灰烬次数，仅诊断不入档
	bool searchPaidDefensesRetained = false; // 选中的完整玩家应对保留付费手动工具，仅诊断不入档
	int searchReinforcementEvaluated = 0; // 组合预算中的跟队增援候选数，仅诊断不入档
	int searchRefinementEvaluated = 0; // 完整优案换入少量其他成员的候选数，仅诊断不入档
	int searchIncomeEvaluated = 0, searchPruningEvaluated = 0; // 经营分支与最终删成员对照，仅诊断不入档
	int searchProposalEvaluated = 0; // 未付款提案跨轮重新评价的次数，仅诊断不入档
	int searchExperiencedEvaluated = 0; // 经验编队与自由搜索共享预算的实际积分次数，仅诊断不入档
	bool searchExperiencedSelected = false; // 最终直接选中经验原案，自由变异后的案不标记为固定编队
	int searchAssaultEvaluated = 0; // 通用攻城中间态深化次数，仅诊断不入档
	float searchCombinationBaseScore = 0, searchCombinationBestScore = 0; // 最终阶段组合比较前后评分
	bool searchCombinationBaseBreach = false, searchCombinationBestBreach = false; // 突破优先于中间收益
	float searchCombinationBaseBreachSeconds = -1, searchCombinationBestBreachSeconds = -1; // 比较前后首次进屋游戏秒，仅诊断不入档
	float lastBestScore = 0.0f;
	std::array<float, 8> searchFeatures{}, searchBaselineFeatures{}; // 同一推演时域的计划/不增援预测（产冰固定60秒），诊断不入档
	float searchPreferenceScore = 0, searchRawPreferenceScore = 0; // 有界/原始兵种先验对评分的贡献，诊断不入档
	float searchOpponentAssets = 0, searchBaselineOpponentAssets = 0, searchOpponentWeight = 0, searchOpponentScore = 0; // 对方终点资产及不增援对照，诊断不入档
	std::array<float, 6> searchStateInputs{}; // 与 StateFeatureCount 同步，只读局势诊断不入档
	std::array<float, 8> searchEffectiveWeights{}; // 局势层调整后的本次评分，诊断不入档
	ColdStorageSearch::CapitalUtilityInputs searchCapitalInputs; // 现金估值的原快照输入，仅诊断，不入档
	bool searchAdaptive = false; // 是否加载可训练局势层，诊断不入档
	bool searchExpandedForecast = false; // 因小队无收益或带已付队列而采用完整 v2 预测，诊断不入档
	int searchCommittedCount = 0, searchQueueEvaluated = 0, searchQueueChanged = 0; // 本次已有队列、重排试验及改动数，仅诊断
	float searchQueueBeforeScore = 0, searchQueueAfterScore = 0; // 相同权重/时域下重排前后评分，仅诊断
	bool searchQueueBeforeBreach = false, searchQueueAfterBreach = false; // 突破排序不能由评分大小代替
	int searchVersion = 1, searchLargestPlan = 0; // 实际搜索版本和最大已评估编队，诊断不入档
	int searchWidestComposition=0; // 已实际比较的新购混编最大兵种数，既有部队和设备不计，诊断不入档
	bool searchNetEconomy = false; // 是否按净冰收益评分，诊断不入档
	bool searchAnticipateBuilding = false; // 实际启用的未来建设预测版本，诊断不入档
	bool searchAnticipateEconomy = false; // 玩家循环经济及后续订货预测是否启用，诊断不入档
	int searchExchangeCards = 0, searchExchanges = 0, searchOrders = 0; // 实际纳入的经济卡槽、预测周转和订单数量，诊断不入档
	float searchExchangeSun = 0, searchExchangeIce = 0, searchOrderSun = 0, searchOrderIce = 0, searchPendingIce = 0; // 玩家预测交易流水，诊断不入档
	int searchConstructionOptions = 0, searchPredictedPlantings = 0; // 合法建设落点与预测建设数，诊断不入档
	int searchSerial = 0; // 每次搜索递增，包含观望决定；仅供训练记录，不入档
	float searchElapsed = 0, searchRawProduction = 0; // 精确决策时刻与未校准预测，仅诊断
	int searchPlanternResponseGear = -1; // 完整对手推演的路灯响应：-1维持现状、0..3固定挡位、4随燃料切挡，仅诊断
	int searchFogCounters = 0; // 本次完整对手推演中的付费关雾次数，仅诊断
	bool searchCounterSpaceReserved = false; // 本次对手选择暂缓补阵，保留反制空位与资金，仅诊断
	int searchCounterShovels = 0; // 本次完整反制世界中为灰烬主动铲除的普通植物数，仅诊断
	float searchCounterShovelAssets = 0; // 主动腾位牺牲的植物资产，冰价，仅诊断
	float searchCounterHoldSeconds = 0; // 本次保守预测采用的玩家清场/主动打击等待习惯，游戏秒，仅诊断
	int searchBurstOptions = 0, searchAttackAuraCount = 0; // 当前能力投影数量，仅诊断
	int searchGrowingPlants = 0, searchCapitalRejected = 0; // 成长火力来源和资金风险候选淘汰数，仅诊断不入档
	float searchCapitalRiskAllowance = 0; // 根据实际净亏损/剩余付费资产计算的风险额度，冰；仅诊断不入档
	std::vector<ZombieType> searchInstantCrushTypes; // 最优搜索快照中具备压扁预测的候选类型，仅诊断不入档
	int searchMovementBoundsApplied = 0; // 最优案中使用出生移速范围的单位数，仅诊断不入档
	int searchGoldenAccelerationSteps = 0, searchGoldenDrumSteps = 0, searchGoldenResidualSteps = 0, searchGoldenMaxStacks = 0; // 最优案中冰道协同实际生效统计，仅诊断不入档
	int searchBurstActivations = 0, searchAuraActivations = 0; // 最优案预测的未来付费次数
	int searchRepairOptions = 0, searchRepairPlants = 0; // 修复能力候选与当前防线来源数
	int searchArmorRepairs = 0, searchPlantRepairs = 0; // 最优案中两方的实际可付修复次数
	float searchArmorRepairIce = 0, searchPlantRepairIce = 0; // 仅预测的修复支出
	float searchAbilityIce = 0; // 最优案未来技能费，不等于当前出兵付款
	int searchAdaptationOptions = 0, searchRitualOptions = 0; // 适应和裂隙能力的合法采购选项
	int searchRitualReleases = 0, searchRiftSummons = 0, searchRiftRedirects = 0; // 预测释放、到场和界碑反制
	int searchDrumOptions = 0, searchDrumBeats = 0, searchDrumRecipients = 0; // 数值预测，不在正式场景施加效果
	int searchDeploymentSniperOptions = 0, searchDeploymentShots = 0, searchDeploymentHits = 0; // 落种压制候选与预测弹道，不触发正式射击
	int searchClockOptions = 0, searchClockAnchors = 0, searchClockTargets = 0; // 钟匠候选和预测锚覆盖，仅诊断
	int searchClockRewinds = 0, searchClockRevivals = 0, searchClockRedirects = 0; // 预测回溯、复活与界碑拒入，仅诊断
	int searchEliteReplacementOptions = 0, searchEliteRemainingUses = 0; // 共享累计名额的补菇画像，仅诊断
	int searchPrecisionEvaluated = 0, searchPrecisionTargetID = 0; // 本次精准清除搜索
	std::vector<int> searchPrecisionAdditionalTargetIDs; // 完整胜出名单的附加目标，仅诊断，不入档
	int searchForecastPrecisionTargetID = 0, searchForecastPrecisionIce = 0; // 解锁后尚未付款的狙击反事实，仅诊断
	std::vector<int> searchForecastPrecisionAdditionalTargetIDs; // 未来目标不锁定实体、不入档，下轮按真实局面重选
	float searchForecastPrecisionAimStartSeconds = 0; // 相对本次搜索的预计未来瞄准开始时刻，游戏秒
	float searchPrecisionGain = 0; // 相对不施法优案的收益，仅诊断
	int searchRowStrikeCount = 0; // 本次推演纳入的逐行主动打击来源数，仅诊断
	float searchFormationBaseScore = 0; // 逐行集中增援比较前的评分，仅诊断不入档
	std::array<float, 6> searchFormationScores{}; // 同一队伍投向各行的评分，按 tested 位掩码读取
	int searchFormationTested = 0, searchFormationRejected = 0, searchFormationChosenRow = -1; // 合法/亏损行掩码及改选行
	bool unlockProbe = false; // 空场小额出兵可支付后续兵种的完整解锁路径，仅诊断
	std::array<float, 10> searchProductionInputs{}; // 与 ProductionFeatureCount 同步，诊断不入档
	// 本轮派兵解释，仅供观测；由下一次决策重算，不作为存档中的权威玩法状态。
	std::string commanderMode = "opening";
	std::string commanderStrategy = "balanced"; // 根据当前发展与收益重算，不入档
	float spendingHorizon = 120.0f; // 当前库存支出时域，游戏秒，诊断不入档
	float playerGrowthDps = 0.0f; // 玩家一分钟内可补阵火力，诊断不入档
	float predictedKillIncome = 0.0f; // 最优集中进攻的击杀返冰预测，不提前到账
	int economicFollowups = 0; // 本次进攻/突破后跟进工人数，不是同时总上限
	std::array<float, 6> raidNetByRow{}; // 各路整批进攻净收益，诊断不入档
	std::array<float, 6> splashRiskByRow{}; // 本轮各路候选引起的最大额外队友损失，等效冰量，诊断不入档
	int commanderBudget = 0;
	int commanderSpent = 0;
	int commanderReserve = 0;
	int commanderFocusRow = -1;
	float responseWindow = 0.0f;
	bool attackDeferred = false; // 当前决策暂缓下一梯队；实际重评倒计时由 decisionRemaining 保存
	float formationBlastLoss = 0.0f; // 本次已购队伍涉及的最大单爆区投资损失，冰块，诊断不入档
	bool battleStarted = false;
	std::array<float, 4> habits{}; // 经济、爆炸、控制、保护的近期落种偏好，非难度倍率
	std::vector<ColdStorageDeployment> pending;
	// 只在付费队伍首次入场时登记；结算后移除，回溯/读档重建实体不会重新登记。
	std::map<int, int> refundableCosts; // 稳定僵尸ID -> 实际支付冰价，缺项表示免费或已经结算
};
