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
	int killIncome = 0;
	int playerKillIncome = 0; // 玩家通过消灭付费敌人累计回收的冰块
	int deployments = 0;
	int decisions = 0;
	int lastAttackRow = -1;
	int candidatesEvaluated = 0;
	float lastBestScore = 0.0f;
	// 本轮派兵解释，仅供观测；由下一次决策重算，不作为存档中的权威玩法状态。
	std::string commanderMode = "opening";
	int commanderBudget = 0;
	int commanderSpent = 0;
	int commanderReserve = 0;
	int commanderFocusRow = -1;
	float responseWindow = 0.0f;
	bool battleStarted = false;
	std::array<float, 4> habits{}; // 经济、爆炸、控制、保护的近期落种偏好，非难度倍率
	std::vector<ColdStorageDeployment> pending;
	// 只在付费队伍首次入场时登记；结算后移除，回溯/读档重建实体不会重新登记。
	std::map<int, int> refundableCosts; // 稳定僵尸ID -> 实际支付冰价，缺项表示免费或已经结算
};
