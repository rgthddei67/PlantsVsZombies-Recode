#pragma once

#include "Game/Zombie/ZombieType.h"
#include <array>
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
	int spent = 0;
	int supplied = 0;
	int killIncome = 0;
	int deployments = 0;
	int decisions = 0;
	int lastAttackRow = -1;
	int candidatesEvaluated = 0;
	float lastBestScore = 0.0f;
	bool battleStarted = false;
	std::array<float, 4> habits{}; // 经济、爆炸、控制、保护的近期落种偏好，非难度倍率
	std::vector<ColdStorageDeployment> pending;
};
