#pragma once

#include "Game/Zombie/ZombieType.h"
#include <array>
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
};

/** 最近经营窗口中的实际收支；正数金额，补给与预测收益不在此登记。 */
struct ColdStorageCashFlow {
	float at = 0.0f;
	int production = 0;
	int spent = 0;
};

/** 训练用实际产冰事件；批次排除后来工人的收入，不参与钱包或存档。 */
struct ColdStorageProductionEvent { float at = 0; int wave = 0, amount = 0; };

/** Board 独占的冰块经济与指挥官状态；展示层只读，不另存资源余额。 */
struct ColdStorageState {
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
	float incomeIdleSeconds = 0.0f; // 连续没有实际制冰/击杀收入的游戏秒；定时补给和派兵不重置，入档
	float plantKillIdleSeconds = 0.0f; // 连续没有消灭植物的游戏秒；新局/无历史旧档从零计时，入档
	std::deque<ColdStorageCashFlow> incomeWindow; // 最近经营窗口的实际制冰及购买事务，入档；Update 移除过期记录
	float assaultCooldown = 0.0f; // 总攻后的重新组织时间，游戏秒；读档不重置
	float dispatchQuietSeconds = 0.0f; // 距上次正式派兵的游戏秒；旧 AI 使用，学习分支不据此强迫出兵
	int spent = 0;
	int supplied = 0;
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
	float lastBestScore = 0.0f;
	std::array<float, 8> searchFeatures{}, searchBaselineFeatures{}; // 同一推演时域的计划/不增援预测（产冰固定60秒），诊断不入档
	float searchPreferenceScore = 0; // 兵种经验对本次评分的贡献，诊断不入档
	float searchOpponentAssets = 0, searchBaselineOpponentAssets = 0, searchOpponentWeight = 0, searchOpponentScore = 0; // 对方终点资产及不增援对照，诊断不入档
	std::array<float, 6> searchStateInputs{}; // 与 StateFeatureCount 同步，只读局势诊断不入档
	std::array<float, 8> searchEffectiveWeights{}; // 局势层调整后的本次评分，诊断不入档
	bool searchAdaptive = false; // 是否加载可训练局势层，诊断不入档
	bool searchExpandedForecast = false; // 因小队无收益或带已付队列而采用完整 v2 预测，诊断不入档
	int searchCommittedCount = 0, searchQueueEvaluated = 0, searchQueueChanged = 0; // 本次已有队列、重排试验及改动数，仅诊断
	float searchQueueBeforeScore = 0, searchQueueAfterScore = 0; // 相同权重/时域下重排前后评分，仅诊断
	int searchVersion = 1, searchLargestPlan = 0; // 实际搜索版本和最大已评估编队，诊断不入档
	bool searchNetEconomy = false; // 是否按净冰收益评分，诊断不入档
	bool searchAnticipateBuilding = false; // 实际启用的未来建设预测版本，诊断不入档
	bool searchAnticipateEconomy = false; // 玩家循环经济及后续订货预测是否启用，诊断不入档
	int searchExchangeCards = 0, searchExchanges = 0, searchOrders = 0; // 实际纳入的经济卡槽、预测周转和订单数量，诊断不入档
	float searchExchangeSun = 0, searchExchangeIce = 0, searchOrderSun = 0, searchOrderIce = 0, searchPendingIce = 0; // 玩家预测交易流水，诊断不入档
	int searchConstructionOptions = 0, searchPredictedPlantings = 0; // 合法建设落点与预测建设数，诊断不入档
	int searchSerial = 0; // 每次搜索递增，包含观望决定；仅供训练记录，不入档
	float searchElapsed = 0, searchRawProduction = 0; // 精确决策时刻与未校准预测，仅诊断
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
