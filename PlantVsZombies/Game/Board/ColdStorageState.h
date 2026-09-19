#pragma once

#include "Game/Zombie/ZombieType.h"
#include <array>
#include <map>
#include <string>
#include <vector>

/** 冷藏站出兵的已付款事务；出生之前仍占敌方兵力和同时名额。 */
struct ColdStorageDeployment {
	ZombieType type = ZombieType::ZOMBIE_NORMAL;
	int row = 0;
	int cost = 0;
	float remaining = 0.0f;
};

/** Board 独占的冰块经济与指挥官状态；展示层只读，不另存资源余额。 */
struct ColdStorageState {
	int playerIce = 200; // 开局冷库可支撑完整五路基础阵型，后续依赖采购
	int enemyIce = 0;
	int initialEnemyIce = 0;
	int difficulty = 1;
	int orderIce = 0;
	float orderRemaining = 0.0f;
	float supplyRemaining = 30.0f;
	float decisionRemaining = 45.0f; // 首轮进攻前的布阵时间，游戏秒
	float elapsed = 0.0f;
	float assaultCooldown = 0.0f; // 总攻后的重新组织时间，游戏秒；读档不重置
	float dispatchQuietSeconds = 0.0f; // 距上次正式派兵的游戏秒，限制观望的最长时间
	int spent = 0;
	int supplied = 0;
	int workerIncome = 0; // 制冰工累计为敌方生产的冰块
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
