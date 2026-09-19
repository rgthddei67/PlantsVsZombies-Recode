#pragma once

#include <vector>

/** 冷藏站战略层只消费 Board 提供的可见数值，不读取实体、不改变资源或正式随机序列。 */
namespace ColdStorageStrategy {
struct Target {
	float x = 0.0f;
	float health = 0.0f;
	float attackDps = 0.0f;
	float reward = 0.0f;
};

struct Raid {
	std::vector<Target> targets; // 已按前线到后排排列，同格普通层/保护层合并
	float spawnX = 0.0f;
	float speed = 0.0f;
	float health = 0.0f;
	float cost = 0.0f;
	float guardHealth = 0.0f;
	float directDps = 0.0f;
	float splashDps = 0.0f;
	float slowFactor = 1.0f;
	float responseWindow = 60.0f;
	float smashSeconds = 0.0f; // 零表示常规啃食，正数表示破坏一格的等效砸击时间
	int count = 1;
};

struct RaidResult {
	float income = 0.0f;
	float net = 0.0f; // 击杀返冰减购兵支出，并折损预计送给玩家的返冰
	float remainingHealth = 0.0f;
	int cellsBroken = 0;
};

/** 对同一路的整批进攻估算前进、承伤、破格和击杀收益；不提前发放预测收入。 */
RaidResult ForecastRaid(const Raid& raid);

struct Situation {
	float defenseDps = 0.0f;
	float defenseHealth = 0.0f;
	float playerGrowthDps = 0.0f; // 当前卡槽/资源约束下未来一分钟可增加的火力
	float productionIncome = 0.0f; // 已有工人未来一分钟预计收入
	float investmentNet = 0.0f;
	float raidNet = 0.0f;
	int raidCellsBroken = 0;
	int rows = 5;
};

struct Policy {
	const char* name = "balanced";
	float spendingHorizon = 120.0f; // 将当前库存分摊到未来多久，游戏秒，不是关卡结束时间
	bool racePlayer = false;
	bool buildEconomy = false;
	bool siege = false;
};

/** 比较玩家补阵、己方生产和当前破阵收益，选择抢攻、经营或集中攻坚。 */
Policy Choose(const Situation& situation);
}
